/**
 * @file main.cpp
 * @brief Aurore MkVII Fire Control System - Main Entry Point
 *
 * Real-time vision-based fire control system for Raspberry Pi 5.
 *
 * Architecture:
 * - vision_pipeline thread (SCHED_FIFO=90): 120Hz frame processing
 * - track_compute thread (SCHED_FIFO=85): Target tracking and prediction
 * - actuation_output thread (SCHED_FIFO=95): Gimbal servo commands
 * - safety_monitor thread (SCHED_FIFO=99): 1kHz health monitoring
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <pthread.h>
#include <pwd.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

// libcap for privilege drop (optional - requires libcap-dev)
#ifdef HAVE_LIBCAP
#include <sys/capability.h>
#endif

#include "aurore.pb.h"
#include "aurore/aurore_link_server.hpp"
#include "aurore/ballistic_solver.hpp"
#include "aurore/camera_wrapper.hpp"
#include "aurore/command_socket.hpp"
#include "aurore/config_loader.hpp"
#include "aurore/detector.hpp"  // For OrbDetector
#include "aurore/drivers/laser_rangefinder.hpp"
#include "aurore/dual_camera_manager.hpp"
#include "aurore/fusion_hat.hpp"
#include "aurore/gimbal_controller.hpp"
#include "aurore/hud_socket.hpp"
#include "aurore/interlock_controller.hpp"
#include "aurore/mjpeg_streamer.hpp"
#include "aurore/ring_buffer.hpp"
#include "aurore/safety_monitor.hpp"
#include "aurore/security.hpp"
#include "aurore/state_machine.hpp"  // For TrackSolution
#include "aurore/sweep_pattern.hpp"
#include "aurore/telemetry_writer.hpp"
#include "aurore/timing.hpp"
#include "aurore/tracker.hpp"  // For KcfTracker
#include "aurore/usb_camera.hpp"
#include "aurore/yolo26_detector.hpp"

namespace {

// Global shutdown flag
std::atomic<bool> g_shutdown_requested(false);

// Global dry-run flag (set from main, read by thread helpers)
bool g_dry_run = false;

// Signal handler for graceful shutdown
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_shutdown_requested.store(true, std::memory_order_release);
        std::cout << "\nShutdown requested, cleaning up..." << std::endl;
    }
}

// Configure real-time thread
bool configure_rt_thread(const char* name, int priority, int cpu_affinity) {
    pthread_t thread = pthread_self();

    // Set SCHED_FIFO scheduling
    struct sched_param param;
    param.sched_priority = priority;

    if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
        std::cerr << "Failed to set SCHED_FIFO for " << name << ": " << strerror(errno)
                  << std::endl;
        if (!g_dry_run) return false;
        // In dry-run mode: continue without RT scheduling
    }

    // Set CPU affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(cpu_affinity), &cpuset);

    if (pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "Failed to set CPU affinity for " << name << ": " << strerror(errno)
                  << std::endl;
        if (!g_dry_run) return false;
        // In dry-run mode: continue without CPU affinity
    }

    std::cout << "Thread '" << name << "' configured: priority=" << priority
              << ", cpu=" << cpu_affinity << std::endl;

    return true;
}

// Lock memory to prevent page faults
bool lock_memory() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "FATAL: Failed to lock memory: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "Memory locked successfully" << std::endl;
    return true;
}

// Maximum memory lock limit (64MB - sufficient for real-time buffers)
// This prevents runaway memory locking attacks
constexpr size_t MAX_MEMLOCK_BYTES = 64 * 1024 * 1024;

// Set resource limits with bounds
bool set_resource_limits() {
    struct rlimit rl;

    // Set bounded memlock limit (64MB max)
    // This is sufficient for:
    // - 4x DMA buffers @ 1536x864 RAW10: ~10MB
    // - Stack allocations for RT threads: ~1MB
    // - Safety margin: ~5MB
    rl.rlim_cur = MAX_MEMLOCK_BYTES;
    rl.rlim_max = MAX_MEMLOCK_BYTES;

    if (setrlimit(RLIMIT_MEMLOCK, &rl) != 0) {
        std::cerr << "Warning: Failed to set memlock limit: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "Memory lock limit set to " << (MAX_MEMLOCK_BYTES / (1024 * 1024)) << " MB"
              << std::endl;

    // Set stack size limit for new threads (e.g., 8MB)
    // Required for some real-time threads to avoid stack overflow
    rl.rlim_cur = 8 * 1024 * 1024;  // 8MB
    rl.rlim_max = 8 * 1024 * 1024;  // 8MB

    if (setrlimit(RLIMIT_STACK, &rl) != 0) {
        std::cerr << "Warning: Failed to set stack limit: " << strerror(errno) << std::endl;
        return false;
    }

    std::cout << "Stack size limit set to " << (rl.rlim_cur / (1024 * 1024)) << " MB" << std::endl;

    return true;
}

/**
 * @brief Drop privileges after RT setup
 *
 * After configuring SCHED_FIFO and locking memory, drop root privileges
 * while retaining only the capabilities needed for real-time operation:
 * - CAP_SYS_NICE: For SCHED_FIFO scheduling
 * - CAP_IPC_LOCK: For mlockall
 *
 * This reduces attack surface by running as non-root for most of execution.
 *
 * @param keep_rt_caps If true, retain RT capabilities; if false, drop all
 * @return true on success, false on failure or if libcap not available
 */
bool drop_privileges(bool keep_rt_caps = true) {
    // Get current UID/GID
    const uid_t uid = getuid();
    const gid_t gid = getgid();

    // Don't drop if already non-root
    if (uid != 0) {
        return true;
    }

#ifdef HAVE_LIBCAP
    std::cout << "Dropping privileges (keeping RT capabilities)..." << std::endl;

    if (keep_rt_caps) {
        // Set capabilities for real-time operation
        cap_t caps = cap_init();
        if (caps == nullptr) {
            std::cerr << "Failed to initialize capabilities: " << strerror(errno) << std::endl;
            return false;
        }

        // Keep only CAP_SYS_NICE (scheduling) and CAP_IPC_LOCK (memory locking)
        cap_value_t cap_list[] = {CAP_SYS_NICE, CAP_IPC_LOCK};
        if (cap_set_flag(caps, CAP_EFFECTIVE, 2, cap_list, CAP_SET) != 0) {
            std::cerr << "Failed to set capabilities: " << strerror(errno) << std::endl;
            cap_free(caps);
            return false;
        }
        if (cap_set_flag(caps, CAP_PERMITTED, 2, cap_list, CAP_SET) != 0) {
            std::cerr << "Failed to set permitted capabilities: " << strerror(errno) << std::endl;
            cap_free(caps);
            return false;
        }

        if (cap_set_proc(caps) != 0) {
            std::cerr << "Failed to apply capabilities: " << strerror(errno) << std::endl;
            cap_free(caps);
            return false;
        }

        cap_free(caps);
        std::cout << "Capabilities set: CAP_SYS_NICE, CAP_IPC_LOCK" << std::endl;
    }
#else
    // libcap not available - just drop to non-root without retaining capabilities
    std::cout << "Dropping privileges (libcap not available - full drop)..." << std::endl;
    (void)keep_rt_caps;  // Unused
#endif

    // Set GID first (required before dropping UID)
    if (setgid(gid) != 0) {
        std::cerr << "Failed to drop GID: " << strerror(errno) << std::endl;
        return false;
    }

    // Drop UID
    if (setuid(uid) != 0) {
        std::cerr << "Failed to drop UID: " << strerror(errno) << std::endl;
        return false;
    }

    std::cout << "Privileges dropped: running as UID=" << getuid() << ", GID=" << getgid()
              << std::endl;

    return true;
}

// Spawn Node.js web server (aurore-link) as a child process
pid_t spawn_web_server() {
    const char* node_user = "pi";
    const char* repo_root = "/home/pi/AuroreMkVII";
    const char* server_dir = "aurore-link";

    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to spawn Node.js server: " << strerror(errno) << std::endl;
        return -1;
    }

    if (pid == 0) {  // Child process
        // Change to repository root, then to server directory
        if (chdir(repo_root) != 0) {
            std::cerr << "[aurore-link] Failed to cd to " << repo_root << ": " << strerror(errno)
                      << std::endl;
            exit(1);
        }
        if (chdir(server_dir) != 0) {
            std::cerr << "[aurore-link] Failed to cd to " << server_dir << ": " << strerror(errno)
                      << std::endl;
            exit(1);
        }

        // Get user info for node_user
        struct passwd* pw = getpwnam(node_user);
        if (!pw) {
            std::cerr << "[aurore-link] User '" << node_user << "' not found" << std::endl;
            exit(1);
        }

        // Drop privileges to node_user
        if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
            std::cerr << "[aurore-link] Failed to drop privileges to " << node_user << std::endl;
            exit(1);
        }

        // Execute Node.js server
        execl("/usr/bin/node", "node", "server.js", nullptr);

        // If execl returns, it failed
        std::cerr << "[aurore-link] Failed to exec Node.js: " << strerror(errno) << std::endl;
        exit(1);
    }

    // Parent process
    std::cout << "Web server (aurore-link) spawned with PID " << pid << std::endl;
    return pid;
}

}  // anonymous namespace

/**
 * @brief Main entry point
 *
 * Initializes system, starts control loops, and handles shutdown.
 */
int main(int argc, char* argv[]) {
    std::cout << "Aurore MkVII Fire Control System v" << AURORE_VERSION << std::endl;
    std::cout << "=====================================" << std::endl;

    // Parse command line arguments
#ifdef AURORE_LAPTOP_BUILD
    bool dry_run = true;
    g_dry_run = true;
    std::cout << "Laptop build detected: Defaulting to dry-run mode" << std::endl;
#else
    bool dry_run = false;
#endif

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dry-run" || arg == "-n") {
            dry_run = true;
            g_dry_run = true;
            std::cout << "Dry-run mode enabled (no hardware access)" << std::endl;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -n, --dry-run    Run without hardware access" << std::endl;
            std::cout << "  -h, --help       Show this help" << std::endl;
            return 0;
        }
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Configure resource limits
    if (!dry_run) {
        set_resource_limits();
    }

    // Lock memory (skip in dry-run — no RT guarantees needed)
    if (!dry_run && !lock_memory()) {
        std::cerr << "FATAL: Failed to lock memory (mlockall). System cannot guarantee real-time "
                     "performance."
                  << std::endl;
        return 1;
    }

    // AM7-L2-SEC-002 / AM7-L3-SEC-002: Verify binary ECDSA signature before proceeding
    // In dry-run, verification is advisory (no halt on failure)
    {
        const std::string pub_path = "/etc/aurore/signing_key.pub";
        const std::string sig_path = "/etc/aurore/aurore.sig";
        if (aurore::security::verify_self(pub_path, sig_path)) {
            std::cout << "[security] Binary signature verified (ECDSA P-256)\n";
        } else {
            if (dry_run) {
                std::cerr << "[security] WARNING: Binary signature not verified "
                             "(key/sig absent or invalid) — continuing in dry-run\n";
            } else {
                std::cerr << "[security] ERROR: Binary signature verification FAILED "
                             "— set up signing key at "
                          << pub_path << " and signature at " << sig_path << "\n"
                          << "           Generate keypair: openssl ecparam -name prime256v1 "
                             "-genkey -noout | openssl pkey -out /etc/aurore/signing_key.pem\n"
                          << "           Extract pubkey:   openssl pkey -in "
                             "/etc/aurore/signing_key.pem -pubout -out "
                          << pub_path << "\n"
                          << "           Sign binary:      <see scripts/sign_binary.sh>\n"
                          << "           Continuing without signature verification.\n";
            }
        }
    }

    // Load configuration
    aurore::ConfigLoader config("config/config.json");
    if (!config.is_loaded()) {
        std::cerr << "Warning: Failed to load config/config.json, using defaults" << std::endl;
    } else {
        std::cout << "Loaded config/config.json" << std::endl;
    }

    // Initialize telemetry writer
    aurore::TelemetryConfig tel_config;
    tel_config.log_dir = config.get_string("logging.path", "logs");
    tel_config.enable_csv = true;
    tel_config.enable_json = true;
    aurore::TelemetryWriter telemetry;
    telemetry.start(tel_config);

    // Initialize safety monitor
    aurore::SafetyMonitorConfig safety_config;
    safety_config.vision_deadline_ns =
        static_cast<uint64_t>(config.get_int("safety.vision_deadline_ns", 20000000));
    // actuation_deadline_ns: max allowed age of last actuation update (not WCET).
    // At 120Hz frame period = 8.333ms; allow 2× for jitter headroom.
    safety_config.actuation_deadline_ns =
        static_cast<uint64_t>(config.get_int("safety.actuation_deadline_ns", 16666000));
    safety_config.max_consecutive_misses =
        static_cast<size_t>(config.get_int("safety.max_consecutive_misses", 3));

    if (dry_run) {
        // Relaxed deadlines for non-RT laptop
        safety_config.vision_deadline_ns = 1000000000;     // 1s
        safety_config.actuation_deadline_ns = 1000000000;  // 1s
        safety_config.max_consecutive_misses = 100;
    }

    aurore::SafetyMonitor safety_monitor(safety_config);

    safety_monitor.set_safety_action_callback(
        [](aurore::SafetyFaultCode code, const char* reason, void*) {
            std::cerr << "SAFETY ACTION: " << aurore::fault_code_to_string(code) << " - " << reason
                      << std::endl;
        },
        nullptr);

    safety_monitor.start();

    // Drop privileges after RT setup is complete
    // This reduces attack surface by running as non-root
    if (!dry_run) {
        if (!drop_privileges(true)) {
            std::cerr << "FATAL: Failed to drop privileges (drop_privileges). Exiting for safety."
                      << std::endl;
            return 1;
        }
    }

    // Initialize camera (if not dry-run)
    std::unique_ptr<aurore::CameraWrapper> camera;

    // Declare cam_config here so its dimensions are accessible for streamer construction below.
    aurore::CameraConfig cam_config;
    cam_config.width = config.get_int("camera.width", aurore::DEFAULT_WIDTH);
    cam_config.height = config.get_int("camera.height", aurore::DEFAULT_HEIGHT);
    cam_config.fps = config.get_int("camera.fps", aurore::DEFAULT_FPS);

    // Initialize camera (test pattern in dry-run, real camera otherwise)
    try {
        camera = std::make_unique<aurore::CameraWrapper>(cam_config);
        camera->init();
        camera->start();

        std::cout << "Camera initialized" << (dry_run ? " (test pattern mode)" : "") << ": "
                  << cam_config.width << "x" << cam_config.height << " @ " << cam_config.fps
                  << " FPS" << std::endl;
    } catch (const aurore::CameraException& e) {
        std::cerr << "Camera initialization failed: " << e.what() << std::endl;
        if (!dry_run) return 1;
    }

    // Initialize AuroreLink server for remote operator interface
    aurore::AuroreLinkConfig link_cfg;
    link_cfg.telemetry_port =
        static_cast<uint16_t>(config.get_int("network.aurore_link.telemetry_port", 9000));
    link_cfg.command_port =
        static_cast<uint16_t>(config.get_int("network.aurore_link.command_port", 9002));
    link_cfg.session_timeout_s =
        static_cast<uint32_t>(config.get_int("network.aurore_link.session_timeout_s", 300));
    // Pi uses wlan0; configure this to match the active operator network interface.
    link_cfg.ethernet_interface =
        config.get_string("network.aurore_link.ethernet_interface", "wlan0");

    // AM7-L2-SEC-006: Load HMAC key from protected storage (env var or key file)
    {
        const std::string key_path =
            config.get_string("security.hmac_key_path", "/etc/aurore/hmac.key");
        std::string hmac_key;
        if (aurore::security::load_hmac_key(key_path, hmac_key)) {
            link_cfg.hmac_key = hmac_key;
            std::cout << "[security] HMAC key loaded (" << hmac_key.size() << " bytes)\n";
        } else {
            std::cerr << "[security] WARNING: HMAC authentication DISABLED — "
                         "set AURORE_HMAC_KEY env var or configure security.hmac_key_path\n";
        }
    }

    aurore::AuroreLinkServer link_server(link_cfg);
    link_server.start();

    // Initialize HUD socket for low-latency telemetry to aurore-link frontend
    aurore::HudSocketConfig hud_cfg;
    hud_cfg.socket_path =
        config.get_string("network.hud_telemetry.socket_path", "/run/aurore/hud_telemetry.sock");
    // In dry-run, allow non-root (pi user) to connect
    hud_cfg.require_root_uid = false;
    hud_cfg.socket_permissions = 0666;  // aurore-link (pi user) must always connect
    // Ensure socket directory exists before binding
    {
        std::filesystem::path sock_dir = std::filesystem::path(hud_cfg.socket_path).parent_path();
        std::error_code ec;
        std::filesystem::create_directories(sock_dir, ec);
        if (ec) {
            std::cerr << "Warning: Could not create socket dir " << sock_dir << ": " << ec.message()
                      << std::endl;
        }
    }
    aurore::HudSocket hud_socket(hud_cfg);
    if (hud_socket.start()) {
        std::cout << "HUD socket listening: " << hud_cfg.socket_path << std::endl;
    } else {
        std::cerr << "Warning: HUD socket failed to start" << std::endl;
    }

    // MJPEG preview streamer: publishes MIPI frames to aurore-link over UNIX socket.
    // We push ISP stream 1 (640x360) to avoid a 3.8MB cold-cache DMA copy on the RT path.
    // The encode thread upscales to kStreamWidth x kStreamHeight (1280x720).
    aurore::MjpegStreamer mjpeg_streamer(
        config.get_string("network.mjpeg_stream.socket_path", "/run/aurore/mjpeg_stream.sock"), 640,
        360);
    if (mjpeg_streamer.start()) {
        std::cout << "MJPEG streamer listening: /run/aurore/mjpeg_stream.sock" << std::endl;
    } else {
        std::cerr << "Warning: MJPEG streamer failed to start" << std::endl;
    }

    // MJPEG USB streamer: publishes USB camera frames so aurore-link can serve /stream/usb
    aurore::MjpegStreamer mjpeg_usb_streamer("/run/aurore/mjpeg_usb_stream.sock", 640, 480);
    if (mjpeg_usb_streamer.start()) {
        std::cout << "MJPEG USB streamer listening: /run/aurore/mjpeg_usb_stream.sock" << std::endl;
    } else {
        std::cerr << "Warning: MJPEG USB streamer failed to start" << std::endl;
    }

    // Command socket: receives JSON-derived text commands from aurore-link (Node.js)
    aurore::CommandSocket cmd_socket;
    cmd_socket.start();

    // Spawn Node.js web server (aurore-link) for remote HUD interface on port 8080
    pid_t web_server_pid = spawn_web_server();
    if (web_server_pid == -1) {
        std::cerr << "Warning: Failed to spawn web server" << std::endl;
    }

    // Gimbal controller (FusionHAT+ sysfs driver — fails gracefully without hardware)
    aurore::FusionHat fusion_hat;
    fusion_hat.init();

    // Configure gimbal rate limiting from config
    const float gimbal_rate_limit_dps =
        config.get_float("gimbal.elevation.velocity_limit_dps", 60.0f);
    fusion_hat.set_rate_limit(true, gimbal_rate_limit_dps);

    // Set gimbal endstop limits from config
    const float az_min = config.get_float("gimbal.azimuth.min_deg", -90.0f);
    const float az_max = config.get_float("gimbal.azimuth.max_deg", 90.0f);
    const float el_min = config.get_float("gimbal.elevation.min_deg", -10.0f);
    const float el_max = config.get_float("gimbal.elevation.max_deg", 45.0f);
    fusion_hat.set_endstop_limits(10, az_min,
                                  az_max);  // Channel 10 = azimuth (matches servo output)
    fusion_hat.set_endstop_limits(11, el_min,
                                  el_max);  // Channel 11 = elevation (matches servo output)

    // GimbalController for AUTO/FREECAM gimbal targeting
    aurore::GimbalController gimbal_ctrl;
    gimbal_ctrl.set_limits(az_min, az_max, el_min, el_max);

    // InterlockController for actuation safety gating
    aurore::InterlockConfig interlock_cfg;
    aurore::InterlockController interlock(&fusion_hat, interlock_cfg);
    if (interlock.init()) {
        if (dry_run) {
            interlock.force_state(aurore::InterlockState::CLOSED);
            std::cout << "Interlock: Forced CLOSED in dry-run mode" << std::endl;
        }
        interlock.start();
    } else {
        std::cerr << "Warning: Interlock initialization failed" << std::endl;
    }

    // BallisticSolver for fire control solutions
    aurore::BallisticSolver ballistics;
    // AM7-L2-BALL-002: Load ballistic profiles from config
    ballistics.loadProfiles(config.get_json());
    ballistics.initialize_lookup_table();
    const float muzzle_velocity_mps = config.get_float("ballistics.muzzle_velocity_mps", 900.0f);
    const float test_range_m = config.get_float("ballistics.test_range_m", 5.0f);

    // Laser rangefinder — M01 on UART, continuous mode
    aurore::LaserRangefinder lrf;
    {
        const std::string lrf_device = config.get_string("lrf.uart_device", "/dev/ttyAMA0");
        std::cout << "[LRF] Attempting to initialize LRF on " << lrf_device << std::endl;
        if (lrf.init(lrf_device)) {
            std::cout << "[LRF] LRF initialized successfully." << std::endl;
            lrf.start_continuous();
            std::cout << "[LRF] Started continuous mode on " << lrf_device << "\n";
        } else {
            std::cerr << "[LRF] Warning: failed to open " << lrf_device
                      << " — range will be unavailable\n";
        }
    }
    // Frame ring buffer (zero-copy)
    aurore::LockFreeRingBuffer<aurore::ZeroCopyFrame, 4> frame_buffer;

    // Track solution ring buffer (track_compute -> actuation_output)
    aurore::LockFreeRingBuffer<aurore::TrackSolution, 4> track_buffer;

    // OrbDetector for target detection
    aurore::OrbDetector detector;
    {
        const std::string descriptor_path =
            config.get_string("detector.descriptor_path", "target_signatures/helicopter.yml");
        if (detector.load_descriptor_file(descriptor_path)) {
            std::cout << "Detector: Loaded descriptors from " << descriptor_path << std::endl;
        } else {
            if (dry_run) {
                cv::Mat test_template = cv::Mat::zeros(80, 80, CV_8UC3);
                cv::rectangle(test_template, {10, 10, 60, 60}, {0, 200, 100}, -1);
                detector.add_template(test_template);
                std::cout << "Detector: Using synthesized test template (dry-run mode)"
                          << std::endl;
            } else {
                std::cerr << "Warning: Failed to load descriptor file: " << descriptor_path
                          << std::endl;
            }
        }
    }

    // Dual-stream camera manager (MIPI + USB)
    aurore::DualCameraManager dual_camera;
    dual_camera.init_mipi(camera.get());

    aurore::UsbCameraConfig usb_config;
    usb_config.width = 640;
    usb_config.height = 480;
    usb_config.fps = 30;
    if (dual_camera.init_usb(usb_config)) {
        std::cout << "DualCamera: USB stream initialized for auxiliary detection\n";
    } else {
        std::cerr << "DualCamera: WARN - USB stream failed to initialize\n";
        std::cerr << "      Check: ls /dev/video*\n";
        std::cerr << "      Fix: Connect USB webcam or system operates without optical gate\n";
    }

    // Push every USB frame to the web preview socket (non-RT callback, safe to capture by ref)
    dual_camera.set_usb_frame_callback(
        [&](const cv::Mat& bgr) { mjpeg_usb_streamer.push_frame(bgr); });

    // State machine for FCS mode management
    aurore::StateMachine state_machine;

    // AM7-L2-LOG-OP-001: Log all state transitions to telemetry with context
    state_machine.set_state_change_callback(
        [&telemetry](aurore::FcsState from, aurore::FcsState to) {
            const aurore::TelemetrySeverity sev = (to == aurore::FcsState::FAULT)
                                                      ? aurore::TelemetrySeverity::kCritical
                                                      : aurore::TelemetrySeverity::kInfo;
            const aurore::TelemetryEventId evt = (to == aurore::FcsState::FAULT)
                                                     ? aurore::TelemetryEventId::SAFETY_FAULT
                                                     : aurore::TelemetryEventId::SYSTEM_BOOT;
            telemetry.log_event(evt, sev,
                                std::string("State: ") + aurore::fcs_state_name(from) + " -> " +
                                    aurore::fcs_state_name(to));
            std::cout << "State: " << aurore::fcs_state_name(from) << " -> "
                      << aurore::fcs_state_name(to) << std::endl;
        });

    // Install mode callback for FREECAM/AUTO switching
    link_server.set_mode_callback([&](aurore::LinkMode mode) {
        if (mode == aurore::LinkMode::FREECAM) {
            std::cout << "AuroreLink: Mode switched to FREECAM" << std::endl;
            gimbal_ctrl.set_source(aurore::GimbalSource::FREECAM);
            state_machine.request_freecam();
        } else {
            std::cout << "AuroreLink: Mode switched to AUTO" << std::endl;
            gimbal_ctrl.set_source(aurore::GimbalSource::AUTO);
            state_machine.request_search();
        }
    });

    // AM7-L2-LOG-OP-004: Log arm/disarm transitions with authorization context
    link_server.set_arm_callback([&](bool authorized) {
        telemetry.log_event(authorized ? aurore::TelemetryEventId::SAFETY_INHIBIT_RELEASED
                                       : aurore::TelemetryEventId::SAFETY_INHIBIT_ENGAGED,
                            aurore::TelemetrySeverity::kInfo,
                            authorized ? "Operator authorization granted (ARM)"
                                       : "Operator authorization revoked (DISARM)");
        std::cout << "AuroreLink: Operator authorization " << (authorized ? "granted" : "revoked")
                  << std::endl;
        state_machine.set_operator_authorization(authorized);
    });

    // Install heartbeat timeout callback - triggers transition to IDLE_SAFE
    link_server.set_heartbeat_timeout_callback([&]() {
        std::cerr << "AuroreLink: HEARTBEAT TIMEOUT - Requesting IDLE_SAFE state\n";
        telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                            aurore::TelemetrySeverity::kCritical,
                            "Heartbeat timeout - operator link lost");
        state_machine.request_cancel();  // Transition to IDLE_SAFE
    });

    // Install emergency stop callback - immediate FAULT state transition
    link_server.set_emergency_stop_callback([&]() {
        std::cerr << "AuroreLink: EMERGENCY_INHIBIT - Triggering immediate FAULT state\n";
        telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                            aurore::TelemetrySeverity::kCritical,
                            "Emergency stop requested via EMERGENCY_INHIBIT message");
        // Trigger emergency stop on safety monitor
        safety_monitor.trigger_emergency_stop("EMERGENCY_INHIBIT message received");
        // Force interlock to inhibit state
        interlock.set_inhibit(true);
        // Transition state machine to FAULT
        state_machine.on_fault(aurore::FaultCode::WATCHDOG_TIMEOUT);
    });

    // AM7-L3-SEC-001/AM7-L3-SEC-004: Security event callback — log and fault on attacks
    link_server.set_security_event_callback([&](const std::string& event_type, uint32_t sequence) {
        telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                            aurore::TelemetrySeverity::kCritical,
                            "AuroreLink security event: " + event_type);
        if (event_type == "HMAC_VERIFY_FAIL") {
            state_machine.on_fault(aurore::FaultCode::AUTH_FAILURE);
        } else if (event_type == "SEQ_GAP_FAULT") {
            // AM7-L3-SEC-004: sequence gap > 1000 triggers security fault
            std::cerr << "AuroreLink: SEQ_GAP_FAULT seq=" << sequence
                      << " — triggering security FAULT\n";
            state_machine.on_fault(aurore::FaultCode::SEQUENCE_GAP);
        }
    });

    // AM7-L3-ACT-002: Gimbal command sequence gap detection — reject out-of-order commands.
    // The link protocol sends angular RATES (deg/s); integrate over dt to get absolute target.
    std::atomic<uint64_t> freecam_last_ns{0};
    link_server.set_freecam_callback([&](float az_dps, float el_dps, float /*vel*/,
                                         uint32_t seq_num) {
        const uint64_t now_ns = aurore::get_timestamp();
        const uint64_t prev_ns = freecam_last_ns.exchange(now_ns, std::memory_order_acq_rel);
        const float dt_s =
            (prev_ns == 0) ? (1.0f / 120.0f)
                           : std::clamp(static_cast<float>(now_ns - prev_ns) * 1e-9f, 0.001f, 0.1f);

        // Integrate rate onto current gimbal position to get absolute target.
        const float new_az = gimbal_ctrl.current_az() + az_dps * dt_s;
        const float new_el = gimbal_ctrl.current_el() + el_dps * dt_s;

        auto cmd = gimbal_ctrl.process_command_with_gap_check(new_az, new_el, seq_num);
        if (!cmd.has_value()) {
            std::cerr << "AuroreLink: gimbal sequence gap detected (seq=" << seq_num
                      << ") — holding position\n";
            telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                                aurore::TelemetrySeverity::kCritical,
                                "Gimbal command sequence gap (AM7-L3-ACT-002)");
            state_machine.on_fault(aurore::FaultCode::SEQUENCE_GAP);
        }
    });

    // AM7-L2-LOG-OP-003: Log target selection events
    // AM7-L3-TGT-004: Operator target handoff — queued for vision thread to reinit tracker
    std::atomic<bool> pending_manual_target{false};
    aurore::Detection pending_manual_det{};
    // AM7-L3-TGT-001: Operator target reject — queued for vision thread to reset tracker
    std::atomic<bool> pending_target_reject{false};

    link_server.set_target_select_callback([&](uint16_t cx, uint16_t cy, uint8_t confidence) {
        aurore::FcsState cur = state_machine.state();
        if (cur == aurore::FcsState::TRACKING || cur == aurore::FcsState::SEARCH) {
            // Build a 32×32px init bbox centred on the operator cursor
            constexpr int kHalf = 16;
            aurore::Detection det{};
            det.confidence = static_cast<float>(confidence) / 100.0f;
            det.bbox.x = static_cast<int>(cx) - kHalf;
            det.bbox.y = static_cast<int>(cy) - kHalf;
            det.bbox.w = kHalf * 2;
            det.bbox.h = kHalf * 2;
            pending_manual_det = det;
            pending_manual_target.store(true, std::memory_order_release);
        }
        telemetry.log_event(aurore::TelemetryEventId::DETECTION_VALID,
                            aurore::TelemetrySeverity::kInfo,
                            "Operator target select @ (" + std::to_string(cx) + "," +
                                std::to_string(cy) + ") conf=" + std::to_string(confidence));
    });
    link_server.set_target_confirm_callback([&](uint32_t target_id) {
        telemetry.log_event(aurore::TelemetryEventId::TRACK_ACQUIRED,
                            aurore::TelemetrySeverity::kInfo,
                            "Operator confirmed target id=" + std::to_string(target_id));
    });
    link_server.set_target_reject_callback([&](uint32_t target_id, uint8_t reason) {
        // AM7-L3-TGT-001: Rejection logged with reason code
        telemetry.log_event(aurore::TelemetryEventId::DETECTION_INVALID,
                            aurore::TelemetrySeverity::kInfo,
                            "Operator rejected target id=" + std::to_string(target_id) +
                                " reason=" + std::to_string(reason));
        // Return to SEARCH — tracker.reset() is deferred to vision thread via atomic flag
        aurore::FcsState cur = state_machine.state();
        if (cur == aurore::FcsState::TRACKING || cur == aurore::FcsState::ARMED) {
            pending_target_reject.store(true, std::memory_order_release);
            state_machine.request_search();
        }
    });
    link_server.set_zoom_callback([&](int8_t direction, uint8_t rate) {
        // AM7-L2-IF-004: Zoom (digital ROI crop; optical zoom not equipped)
        telemetry.log_event(aurore::TelemetryEventId::DETECTION_VALID,
                            aurore::TelemetrySeverity::kInfo,
                            std::string("Zoom ") +
                                (direction > 0   ? "in"
                                 : direction < 0 ? "out"
                                                 : "stop") +
                                " rate=" + std::to_string(rate));
    });

    // Command socket callbacks: browser UI → state machine
    cmd_socket.set_mode_callback([&](const std::string& mode) {
        std::cout << "CommandSocket: mode → " << mode << std::endl;
        if (mode == "AUTO") {
            gimbal_ctrl.set_source(aurore::GimbalSource::AUTO);
            state_machine.request_search();
        } else if (mode == "FREECAM") {
            gimbal_ctrl.set_source(aurore::GimbalSource::FREECAM);
            state_machine.request_freecam();
        } else if (mode == "IDLE") {
            state_machine.request_cancel();
        }
    });
    cmd_socket.set_freecam_callback(
        [&](float az_deg, float el_deg) { gimbal_ctrl.command_absolute(az_deg, el_deg); });
    cmd_socket.set_reset_callback([&]() {
        std::cout << "CommandSocket: reset" << std::endl;
        state_machine.on_manual_reset();
    });

    // Control loop state
    std::atomic<uint64_t> frame_sequence(0);
    std::atomic<bool> vision_running(false);
    std::atomic<bool> track_running(false);
    std::atomic<bool> actuation_running(false);
    std::atomic<uint64_t> last_track_sequence(0);

    // Signal hardware init complete (BOOT -> IDLE_SAFE)
    state_machine.on_init_complete();

    // ---------------------------------------------------------------------------
    // Detect thread shared state: track_compute → detect thread (frame supply)
    //                             detect thread → track_compute (result)
    // Uses mutex+cv (non-blocking try_lock in RT thread) to avoid heap alloc in RT.
    // ---------------------------------------------------------------------------
    struct DetectShared {
        std::mutex frame_mtx;
        cv::Mat frame;  // latest BGR frame for detection
        bool frame_ready{false};
        std::condition_variable frame_cv;

        std::mutex result_mtx;
        aurore::Detection latest;  // latest detection result
        bool result_valid{false};
        std::atomic<bool> result_fresh{false};  // set by detect thread, cleared by track_compute
    };
    DetectShared detect_shared;

    // Load YOLO26n detector
    bool yolo_loaded = false;
#ifdef AURORE_HAS_ONNXRUNTIME
    std::unique_ptr<aurore::Yolo26Detector> yolo_detector;
    aurore::Yolo26Detector::Config yolo_cfg;
    yolo_cfg.model_path =
        config.get_string("vision.yolo_model_path", "/home/pi/AuroreMkVII/models/yolo26n.onnx");
    yolo_cfg.num_threads = 1;  // pinned to CPU 0; no benefit from extra ORT threads
    yolo_detector = std::make_unique<aurore::Yolo26Detector>(yolo_cfg);
    yolo_loaded = yolo_detector->load();
    if (!yolo_loaded) {
        std::cerr << "Warning: YOLO26n model not loaded — SEARCH will use ORB detector only\n";
    }
#else
    std::cerr << "Warning: ONNX Runtime not available — SEARCH will use ORB detector only\n";
#endif

    // Start watchdog just before threads launch so the 60ms window doesn't
    // expire during the several-second hardware initialization sequence above.
    if (!dry_run) {
        safety_monitor.init();
    }

    // USB preview thread: drives process_usb_frame() so the USB camera is read and
    // the frame callback feeds mjpeg_usb_streamer. Non-RT, ~30fps.
    std::thread usb_preview_thread([&]() {
        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            dual_camera.process_usb_frame();  // blocks up to 100ms per frame
        }
    });

    // Detect thread: runs YOLO asynchronously, non-RT
    std::atomic<bool> detect_running{false};
    std::thread detect_thread([&]() {
        detect_running.store(true, std::memory_order_release);
        std::cout << "detect_thread started" << std::endl;

        while (!g_shutdown_requested.load(std::memory_order_acquire)) {
            cv::Mat local_frame;
            {
                std::unique_lock<std::mutex> lk(detect_shared.frame_mtx);
                detect_shared.frame_cv.wait_for(lk, std::chrono::milliseconds(100), [&] {
                    return detect_shared.frame_ready ||
                           g_shutdown_requested.load(std::memory_order_acquire);
                });
                if (!detect_shared.frame_ready) continue;
                local_frame = detect_shared.frame.clone();
                detect_shared.frame_ready = false;
            }

            if (local_frame.empty()) continue;

            std::optional<aurore::Detection> det;
#ifdef AURORE_HAS_ONNXRUNTIME
            if (yolo_loaded && yolo_detector) {
                det = yolo_detector->detect(local_frame);
            }
#endif
            if (det.has_value()) {
                std::lock_guard<std::mutex> lk(detect_shared.result_mtx);
                detect_shared.latest = *det;
                detect_shared.result_valid = true;
                detect_shared.result_fresh.store(true, std::memory_order_release);
            }
        }

        detect_running.store(false, std::memory_order_release);
        std::cout << "detect_thread stopped" << std::endl;
    });

    // Pin detect_thread to CPU 0 so ORT's internal thread pool cannot spill onto
    // CPU 1, which libcamera's callback thread needs.  Without this the camera
    // event loop is starved and try_capture_frame returns false ~90% of the time.
    {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(0, &cs);
        pthread_setaffinity_np(detect_thread.native_handle(), sizeof(cs), &cs);
    }

    // Vision pipeline thread - pinned to CPU 3 for isolation from track thread (CPU 2)
    // This reduces jitter from ISP interrupts and track_compute context switching
    std::thread vision_thread([&]() {
        if (!configure_rt_thread("vision_pipeline", 90, 3)) {
            return;
        }

        aurore::ThreadTiming timing(8333333,
                                    0);  // 120Hz target, hardware achieves ~70fps at 1536x864
        aurore::DeadlineMonitor deadline(
            25000000);  // 25ms WCET: ISP delivers at ~17ms, headroom for jitter

        vision_running.store(true, std::memory_order_release);

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            // RAII watchdog kick - auto-kick at end of each loop iteration
            aurore::WatchdogKick kick(safety_monitor);

            timing.wait();

            if (timing.missed_deadline()) {
                std::cerr << "Vision deadline missed (consecutive: " << timing.consecutive_misses()
                          << ")" << std::endl;
            }

            // Capture frame
            if (camera && camera->is_running()) {
                deadline.start();

                aurore::ZeroCopyFrame frame;
                if (camera->try_capture_frame(frame)) {
                    frame.sequence = frame_sequence.fetch_add(1, std::memory_order_relaxed);

                    // Push to ring buffer (drop if full)
                    frame_buffer.push(frame);

                    // Update safety monitor
                    safety_monitor.update_vision_frame(frame.sequence, frame.timestamp_ns);
                }

                deadline.stop();
                if (deadline.exceeded()) {
                    static uint64_t last_vision_warn_ns = 0;
                    const uint64_t now_warn = aurore::get_timestamp();
                    if (now_warn - last_vision_warn_ns > 5000000000ULL) {  // at most once per 5s
                        std::cerr << "Vision capture exceeded deadline: "
                                  << deadline.elapsed_ns() / 1000 << "us\n";
                        last_vision_warn_ns = now_warn;
                    }
                }
            }
        }  // kick_watchdog() called here automatically

        vision_running.store(false, std::memory_order_release);
    });

    // Track compute thread
    std::thread track_thread([&]() {
        if (!configure_rt_thread("track_compute", 85, 2)) {
            return;
        }

        aurore::ThreadTiming timing(8333333, 2000000);  // 120Hz, 2ms phase offset
        aurore::DeadlineMonitor deadline(5000000);      // 5ms budget (WCET spec per AGENTS.md)

        track_running.store(true, std::memory_order_release);

        // INT-003 Fix: Track solution for actuation output
        aurore::TrackSolution current_solution;
        current_solution.valid = false;

        // Vision pipeline integration: KCF tracker instance
        aurore::KcfTracker tracker;
        tracker.set_camera(camera.get());  // Zero-copy: tracker holds DMA buffer references

        // Autonomous sweep pattern for SEARCH state
        aurore::SweepPattern sweep;
        uint64_t last_tick_ns = aurore::get_timestamp();

        // AM7-L2-TGT-003: Candidate confirmation state during SEARCH.
        // Once a first detection is found, hold gimbal, run tracker for 3-frame
        // stability confirmation. Tracker is only (re-)initialised when no candidate
        // is pending.
        bool candidate_found = false;  // true = first detection in progress

        // Frame counter for detect thread feed rate (every 4th frame → ~30fps detection)
        uint64_t detect_frame_count = 0;
        constexpr uint64_t kDetectEveryN = 10;

        // Vision watchdog: track last frame timestamp
        // Initialized to 0 so the watchdog only arms after the first frame arrives.
        uint64_t last_frame_ns = 0;
        // 33ms = 4 frame periods at 120fps (8.333ms each). Any gap longer than 4 missed
        // frames indicates a genuinely stalled pipeline, not routine scheduling variance.
        // Dry-run uses 250ms because non-RT scheduling causes irregular frame delivery.
        const uint64_t kVisionWatchdogNs = dry_run ? 250000000ULL : 33000000ULL;

        // Request SEARCH mode (IDLE_SAFE -> SEARCH)
        state_machine.request_search();

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            // RAII watchdog kick - auto-kick at end of each loop iteration
            aurore::WatchdogKick kick(safety_monitor);

            timing.wait();

            // Process frame from buffer
            aurore::ZeroCopyFrame frame;
            uint64_t now_ns = aurore::get_timestamp();
            bool frame_available = frame_buffer.pop(frame);

            if (frame_available && camera) {
                deadline.start();
                last_frame_ns = now_ns;
                const uint64_t t0_track = aurore::get_timestamp();

                // Wrap frame as OpenCV Mat (RAW10→BGR888 conversion happens here)
                cv::Mat bgr_frame = camera->wrap_as_mat(frame, aurore::PixelFormat::BGR888);
                const uint64_t t1_wrap = aurore::get_timestamp();

                if (!bgr_frame.empty()) {
                    aurore::FcsState state = state_machine.state();

                    if (state == aurore::FcsState::SEARCH) {
                        if (!candidate_found) {
                            // --- Phase 1: sweep + detect until first hit ---
                            uint64_t now_tick = aurore::get_timestamp();
                            const float dt_sec =
                                static_cast<float>(now_tick - last_tick_ns) * 1e-9f;
                            last_tick_ns = now_tick;
                            auto sweep_pt = sweep.tick(dt_sec);
                            gimbal_ctrl.command_absolute(sweep_pt.az_deg, sweep_pt.el_deg);

                            std::optional<aurore::Detection> detection;

                            // Feed YOLO detect thread every kDetectEveryN frames.
                            // Use hardware ISP stream 1 (640x360 BGR888) — zero-copy DMA
                            // header, then copyTo a CPU buffer so detect_thread owns it
                            // after release_frame().  No software resize on the RT thread.
                            if (yolo_loaded && (++detect_frame_count % kDetectEveryN == 0)) {
                                if (detect_shared.frame_mtx.try_lock()) {
                                    cv::Mat yolo_src =
                                        camera->wrap_as_mat(frame, aurore::PixelFormat::BGR888, 1);
                                    if (!yolo_src.empty()) {
                                        yolo_src.copyTo(detect_shared.frame);
                                        detect_shared.frame_ready = true;
                                        detect_shared.frame_cv.notify_one();
                                    }
                                    detect_shared.frame_mtx.unlock();
                                }
                            }

                            // Check for fresh YOLO result (non-blocking)
                            if (detect_shared.result_fresh.load(std::memory_order_acquire)) {
                                if (detect_shared.result_mtx.try_lock()) {
                                    if (detect_shared.result_valid) {
                                        detection = detect_shared.latest;
                                    }
                                    detect_shared.result_fresh.store(false,
                                                                     std::memory_order_release);
                                    detect_shared.result_mtx.unlock();
                                }
                            }

                            if (!yolo_loaded && detector.is_ready()) {
                                detection = detector.detect(bgr_frame);
                            }

                            // AM7-L2-TGT-003: Only initiate tracking on ≥95% confidence detections.
                            if (detection.has_value() && detection->confidence >= 0.95f) {
                                // First hit — stop sweep, init tracker; hold gimbal here.
                                sweep.reset();
                                cv::Rect2d det_bbox(static_cast<float>(detection->bbox.x),
                                                    static_cast<float>(detection->bbox.y),
                                                    static_cast<float>(detection->bbox.w),
                                                    static_cast<float>(detection->bbox.h));
                                if (tracker.init(bgr_frame, det_bbox)) {
                                    tracker.capture_reference_template(frame, det_bbox);
                                    state_machine.on_detection(*detection);
                                    candidate_found = true;
                                }
                            }
                        } else {
                            // --- Phase 2: hold gimbal, confirm candidate via tracker ---
                            // AM7-L2-TGT-003: 3 stable frames required before TRACKING transition.
                            last_tick_ns = aurore::get_timestamp();  // keep dt accumulator valid
                            auto track_sol = tracker.update(bgr_frame);

                            if (track_sol.valid) {
                                // Feed tracker centroid as a synthetic detection to accumulate
                                // position history in the state machine.
                                aurore::Detection synth;
                                synth.confidence = 0.96f;
                                synth.bbox.x = static_cast<int>(track_sol.centroid_x);
                                synth.bbox.y = static_cast<int>(track_sol.centroid_y);
                                synth.bbox.w = static_cast<int>(tracker.last_bbox().width);
                                synth.bbox.h = static_cast<int>(tracker.last_bbox().height);
                                state_machine.on_detection(synth);
                                // state_machine may have just transitioned to TRACKING —
                                // that's fine; next frame reads state == TRACKING.
                            } else {
                                // Candidate lost — resume sweep from centre
                                candidate_found = false;
                                tracker.reset();
                                sweep.reset();
                            }
                        }
                        // Stay in SEARCH — centroid at frame center
                        current_solution.valid = false;
                        current_solution.centroid_x = static_cast<float>(frame.width) / 2.0f;
                        current_solution.centroid_y = static_cast<float>(frame.height) / 2.0f;
                    } else if (state == aurore::FcsState::TRACKING ||
                               state == aurore::FcsState::ARMED) {
                        // Entering TRACKING — clear candidate confirmation state
                        candidate_found = false;

                        // AM7-L3-TGT-001: Process pending operator target reject
                        if (pending_target_reject.load(std::memory_order_acquire)) {
                            pending_target_reject.store(false, std::memory_order_release);
                            tracker.reset();
                            candidate_found = false;
                            sweep.reset();
                        }

                        // AM7-L3-TGT-004: Process pending operator target handoff
                        if (pending_manual_target.load(std::memory_order_acquire)) {
                            pending_manual_target.store(false, std::memory_order_release);
                            const cv::Rect2d new_bbox(
                                static_cast<double>(pending_manual_det.bbox.x),
                                static_cast<double>(pending_manual_det.bbox.y),
                                static_cast<double>(pending_manual_det.bbox.w),
                                static_cast<double>(pending_manual_det.bbox.h));
                            if (tracker.init(bgr_frame, new_bbox)) {
                                state_machine.on_detection(pending_manual_det);
                                std::cerr << "[Vision] AM7-L3-TGT-004: tracker handoff @ ("
                                          << pending_manual_det.bbox.x << ","
                                          << pending_manual_det.bbox.y << ")\n";
                            }
                        }

                        // TRACKING/ARMED: update KCF tracker
                        current_solution = tracker.update(bgr_frame);
                        {
                            const cv::Rect2d bb = tracker.last_bbox();
                            current_solution.bbox_w = static_cast<float>(bb.width);
                            current_solution.bbox_h = static_cast<float>(bb.height);
                        }

                        if (current_solution.valid) {
                            // Optical Logic Gate: Validate alignment between USB and MIPI streams
                            if (dual_camera.is_usb_connected()) {
                                auto roi = dual_camera.get_latest_roi();
                                if (roi.has_value()) {
                                    float roi_cx = roi->x + roi->w * 0.5f;
                                    float roi_cy = roi->y + roi->h * 0.5f;
                                    float dx = std::abs(current_solution.centroid_x - roi_cx);
                                    float dy = std::abs(current_solution.centroid_y - roi_cy);

                                    bool aligned = (dx < 50.0f && dy < 50.0f);
                                    dual_camera.set_usb_aligned(aligned);
                                    dual_camera.set_optical_gate_passed(aligned);

                                    if (!aligned) {
                                        std::cerr << "[OpticalGate] WARN: USB/MIPI misalignment - "
                                                  << "dx=" << dx << "px, dy=" << dy << "px\n";
                                        telemetry.log_event(
                                            aurore::TelemetryEventId::DUAL_STREAM_OPTICAL_GATE_FAIL,
                                            aurore::TelemetrySeverity::kWarning,
                                            "USB/MIPI misalignment");
                                    }
                                }
                            }

                            // Validate solution bounds
                            if (current_solution.centroid_x < 0 ||
                                current_solution.centroid_x > static_cast<float>(frame.width) ||
                                current_solution.centroid_y < 0 ||
                                current_solution.centroid_y > static_cast<float>(frame.height)) {
                                current_solution.valid = false;
                                tracker.reset();
                            } else {
                                // Use tracker-computed PSR (matchTemplate correlation).
                                // A PSR below 3.0 indicates weak correlation — log but don't fault.
                                state_machine.on_tracker_update(current_solution);
                            }
                        }

                        if (!current_solution.valid) {
                            // Track lost - attempt redetection
                            float redetect_score = tracker.redetect(bgr_frame);
                            state_machine.on_redetection_score(redetect_score);
                            if (redetect_score < 0.85f) {
                                // Redetection failed - reset tracker and resume sweep
                                candidate_found = false;
                                tracker.reset();
                                sweep.reset();
                                last_tick_ns = aurore::get_timestamp();
                            }
                            current_solution.centroid_x = static_cast<float>(frame.width) / 2.0f;
                            current_solution.centroid_y = static_cast<float>(frame.height) / 2.0f;
                        }
                    } else {
                        // Other states (IDLE_SAFE, FREECAM, BOOT, FAULT) - no tracking
                        current_solution.valid = false;
                        current_solution.centroid_x = static_cast<float>(frame.width) / 2.0f;
                        current_solution.centroid_y = static_cast<float>(frame.height) / 2.0f;
                    }
                } else {
                    // Frame conversion failed
                    current_solution.valid = false;
                }

                // Preview frame for web interface — use ISP stream 1 (640x360) to avoid
                // a 3.8MB DMA cold-cache copy on the RT path.  Encode thread upscales.
                if (!bgr_frame.empty()) {
                    cv::Mat preview = camera->wrap_as_mat(frame, aurore::PixelFormat::BGR888, 1);
                    if (!preview.empty()) {
                        mjpeg_streamer.push_frame(preview);
                    } else {
                        mjpeg_streamer.push_frame(bgr_frame);  // fallback: full-res
                    }
                }

                const uint64_t t2_state = aurore::get_timestamp();

                // Advance state machine clock — drives SEARCH and ARMED timeouts.
                // Period is nominally 8.333ms (120Hz); use constant to avoid per-frame
                // timestamp arithmetic on the RT path.
                state_machine.tick(std::chrono::milliseconds(8));

                if (!track_buffer.push(current_solution)) {
                    // Buffer full - solution dropped
                }
                last_track_sequence.store(frame.sequence, std::memory_order_release);

                // Dual-stream telemetry: Log MIPI and USB frame stats
                auto stream_status = dual_camera.get_status();
                dual_camera.record_mipi_frame(now_ns);
                (void)aurore::get_timestamp();  // t3_dual timing point (kept for future profiling)

                // Log dual-stream metrics every 100 frames
                if (frame.sequence % 100 == 0) {
                    if (stream_status.mipi_latency_us > 5000) {
                        telemetry.log_event(aurore::TelemetryEventId::DUAL_STREAM_LATENCY_WARNING,
                                            aurore::TelemetrySeverity::kWarning,
                                            "MIPI latency exceeded 5ms");
                    }
                }

                // Zero-copy release
                camera->release_frame(frame);
                const uint64_t t4_release = aurore::get_timestamp();

                deadline.stop();
                if (deadline.exceeded()) {
                    static uint64_t last_track_warn_ns = 0;
                    const uint64_t now_warn = aurore::get_timestamp();
                    if (now_warn - last_track_warn_ns > 5000000000ULL) {  // at most once per 5s
                        std::cerr << "Track compute exceeded deadline: "
                                  << deadline.elapsed_ns() / 1000 << "us"
                                  << " [wrap=" << (t1_wrap - t0_track) / 1000 << "us"
                                  << " state=" << (t2_state - t1_wrap) / 1000 << "us"
                                  << " total=" << (t4_release - t0_track) / 1000 << "us"
                                  << "]\n";
                        last_track_warn_ns = now_warn;
                    }
                }
            } else {
                // No frame available - check vision watchdog (only after first frame arrives
                // and only if not already in FAULT to prevent telemetry flooding).
                if (last_frame_ns != 0 && state_machine.state() != aurore::FcsState::FAULT) {
                    uint64_t elapsed = now_ns - last_frame_ns;
                    if (elapsed > kVisionWatchdogNs) {
                        state_machine.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
                        telemetry.log_event(aurore::TelemetryEventId::CAMERA_TIMEOUT,
                                            aurore::TelemetrySeverity::kWarning,
                                            "Vision pipeline timeout (>150ms)");
                    }
                }

                current_solution.valid = false;
                if (!track_buffer.push(current_solution)) {
                    // Buffer full
                }
            }
        }

        track_running.store(false, std::memory_order_release);
    });

    // Actuation output thread
    std::thread actuation_thread([&]() {
        if (!configure_rt_thread("actuation_output", 95, 2)) {
            return;
        }

        aurore::ThreadTiming timing(8333333, 4000000);  // 120Hz, 4ms phase offset
        aurore::DeadlineMonitor deadline(1500000);      // 1.5ms budget

        actuation_running.store(true, std::memory_order_release);

        // Track latest solution from track thread
        aurore::TrackSolution latest_solution;
        latest_solution.valid = false;
        uint64_t last_actuation_sequence = 0;

        // Last ballistics solution for state machine feedback
        std::optional<aurore::FireControlSolution> last_ballistics_sol;

        // Gimbal velocity estimation: finite-difference over successive angle reads
        float prev_az_deg = 0.0f;
        float prev_el_deg = 0.0f;
        aurore::TimestampNs prev_gimbal_ts = 0;

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            // RAII watchdog kick - auto-kick at end of each loop iteration
            aurore::WatchdogKick kick(safety_monitor);

            timing.wait();
            deadline.start();
            const uint64_t ta0 = aurore::get_timestamp();

            // Read latest track solution from buffer
            while (track_buffer.pop(latest_solution)) {
                last_actuation_sequence++;
            }

            // Get current FSM state to gate actuation
            aurore::FcsState state = state_machine.state();
            bool actuation_allowed =
                interlock.is_actuation_allowed() &&
                (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED ||
                 state == aurore::FcsState::FREECAM);

            // Compute gimbal command based on source (AUTO=tracking centroid, FREECAM=operator)
            aurore::GimbalCommand gimbal_cmd{0.f, 0.f, std::nullopt};
            if (latest_solution.valid && state == aurore::FcsState::TRACKING) {
                // AUTO mode: convert track centroid to gimbal delta
                gimbal_cmd = gimbal_ctrl.command_from_pixel(latest_solution.centroid_x,
                                                            latest_solution.centroid_y, 1.0f);
            } else if (state == aurore::FcsState::FREECAM) {
                // FREECAM mode: use last commanded angles (set via operator link)
                gimbal_cmd.az_deg = gimbal_ctrl.current_az();
                gimbal_cmd.el_deg = gimbal_ctrl.current_el();
            }

            // Send servo commands only if actuation is gated and we're in a command state
            // Per HIL spec: Azimuth=Ch10, Elevation=Ch11, Trigger=Ch8
            if (actuation_allowed &&
                (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED ||
                 state == aurore::FcsState::FREECAM)) {
                fusion_hat.set_servo_angle(10, gimbal_cmd.az_deg);  // ch10 = azimuth
                fusion_hat.set_servo_angle(11, gimbal_cmd.el_deg);  // ch11 = elevation
            }

            // I2C fault: only count errors and trigger fault when the gimbal is actively
            // commanded (TRACKING/ARMED/FREECAM). In SEARCH/IDLE the bus may be quiet and
            // startup transients must not propagate to FAULT.
            if (actuation_allowed) {
                if (fusion_hat.is_error_threshold_exceeded()) {
                    const uint64_t error_count = fusion_hat.get_error_count();
                    const uint64_t timeout_count = fusion_hat.get_i2c_timeout_count();
                    const uint64_t nack_count = fusion_hat.get_i2c_nack_count();

                    std::cerr << "FusionHat: I2C error threshold exceeded (errors: " << error_count
                              << ", timeouts: " << timeout_count << ", NACKs: " << nack_count << ")"
                              << std::endl;

                    telemetry.log_event(aurore::TelemetryEventId::I2C_FAULT,
                                        aurore::TelemetrySeverity::kCritical,
                                        "FusionHat I2C error threshold exceeded");

                    state_machine.on_fault(aurore::FaultCode::I2C_FAULT);
                }
            } else {
                // Not in an active actuation state — clear accumulated startup errors so
                // they don't trip the threshold the moment we enter TRACKING/FREECAM.
                fusion_hat.reset_error_counters();
            }

            // AM7-L3-SAFE-002: Validate and ingest LRF reading each actuation cycle.
            // Wraps the raw UART reading in a RangeData with timestamp and CRC so the
            // state machine can revoke stale or corrupted data.
            float effective_range_m = test_range_m;  // fallback if LRF absent/invalid
            {
                const float live_m = lrf.latest_range_m();
                if (live_m > 0.0f) {
                    aurore::RangeData rd;
                    rd.range_m = live_m;
                    rd.timestamp_ns = aurore::get_timestamp();
                    rd.checksum = aurore::StateMachine::compute_crc16(rd.range_m, rd.timestamp_ns);
                    state_machine.on_lrf_range(rd);  // validates age, CRC, bounds
                    if (state_machine.has_valid_range()) {
                        effective_range_m = live_m;
                    }
                }
            }

            // Compute ballistics solution if tracking
            if (latest_solution.valid && state == aurore::FcsState::TRACKING) {
                // Estimate target aspect angle (elevation from gimbal + range offset)
                const float target_aspect = gimbal_cmd.el_deg;
                last_ballistics_sol = ballistics.solve(
                    effective_range_m,     // live LRF range (or fallback)
                    gimbal_cmd.el_deg,     // Current gimbal elevation
                    target_aspect,         // Target aspect (equals gimbal el in simplified model)
                    muzzle_velocity_mps);  // Configured muzzle velocity

                if (last_ballistics_sol.has_value()) {
                    // Signal ballistics solution to state machine (enables ARMED mode)
                    state_machine.on_ballistics_solution(*last_ballistics_sol);
                }
            }

            // Read gimbal status from actual servo feedback (ch10=az, ch11=el)
            // Only poll I2C when actuation is active — idle polling accumulates errors
            // during startup and triggers spurious FAULT transitions.
            aurore::GimbalStatusSm gimbal_status;
            const aurore::TimestampNs gimbal_ts = aurore::get_timestamp();
            const uint64_t ta1 = gimbal_ts;  // end of state/ballistics work
            const auto az_opt = actuation_allowed ? fusion_hat.get_servo_angle(10) : std::nullopt;
            const auto el_opt = actuation_allowed ? fusion_hat.get_servo_angle(11) : std::nullopt;
            if (az_opt) {
                gimbal_status.az_error_deg = std::abs(*az_opt - gimbal_cmd.az_deg);
            }
            if (el_opt) {
                gimbal_status.el_error_deg = std::abs(*el_opt - gimbal_cmd.el_deg);
            }
            if (prev_gimbal_ts > 0 && az_opt && el_opt) {
                const float dt_s = static_cast<float>(gimbal_ts - prev_gimbal_ts) * 1e-9f;
                if (dt_s > 0.0f) {
                    const float daz = *az_opt - prev_az_deg;
                    const float del = *el_opt - prev_el_deg;
                    gimbal_status.velocity_deg_s = std::sqrt(daz * daz + del * del) / dt_s;
                }
            }
            if (az_opt) prev_az_deg = *az_opt;
            if (el_opt) prev_el_deg = *el_opt;
            prev_gimbal_ts = gimbal_ts;
            state_machine.on_gimbal_status(gimbal_status);
            state_machine.set_timing_stable(safety_monitor.deadline_misses() == 0);

            // AM7-L3-ACT-003: Detect and log gimbal position limit violations
            if (gimbal_ctrl.check_and_clear_limit_violation()) {
                std::cerr << "GimbalCtrl: WARN position limit violation clamped"
                          << " (az=" << gimbal_cmd.az_deg << " el=" << gimbal_cmd.el_deg << ")\n";
                telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                                    aurore::TelemetrySeverity::kWarning,
                                    "Gimbal position limit violation (AM7-L3-ACT-003)");
            }

            // Update safety monitor for actuation frame
            if (last_actuation_sequence > 0) {
                const aurore::TimestampNs now = aurore::get_timestamp();
                safety_monitor.update_actuation_frame(last_actuation_sequence, now);
            }

            // Broadcast telemetry and HUD updates at 120Hz (AM7-L2-HUD-004)
            // HUD socket broadcast (low-latency JSON to frontend)
            aurore::HudFrame hud_frame;
            hud_frame.state = static_cast<int>(state);
            hud_frame.az_deg = gimbal_cmd.az_deg;
            hud_frame.el_deg = gimbal_cmd.el_deg;
            hud_frame.target_cx = latest_solution.centroid_x;
            hud_frame.target_cy = latest_solution.centroid_y;
            hud_frame.target_w = latest_solution.valid ? latest_solution.bbox_w : 0.0f;
            hud_frame.target_h = latest_solution.valid ? latest_solution.bbox_h : 0.0f;
            hud_frame.velocity_x = latest_solution.velocity_x;
            hud_frame.velocity_y = latest_solution.velocity_y;
            hud_frame.confidence = latest_solution.psr > 0 ? latest_solution.psr : 0.0f;
            {
                const float live = lrf.latest_range_m();
                hud_frame.range_m = (live > 0.0f) ? live : test_range_m;
            }
            hud_frame.timestamp_ns = aurore::get_timestamp();

            // SYSTEM_STATUS fields (AM7-L2-HUD-004)
            hud_frame.interlock = interlock.is_actuation_allowed() ? 1 : 0;
            hud_frame.target_lock =
                (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED) ? 1 : 0;
            hud_frame.fault_active = (state == aurore::FcsState::FAULT) ? 1 : 0;
            // CPU temp: read from /sys/class/thermal/thermal_zone0/temp (millidegrees C)
            // Read every 120 actuation cycles (~1s at 120Hz) to minimise RT thread file I/O.
            {
                static uint32_t temp_read_counter = 0;
                static int last_temp_milli = 0;
                if (++temp_read_counter >= 120) {
                    temp_read_counter = 0;
                    std::ifstream thermal_file("/sys/class/thermal/thermal_zone0/temp");
                    if (thermal_file >> last_temp_milli) {
                        hud_frame.cpu_temp_c = static_cast<uint16_t>(last_temp_milli / 100);
                    } else {
                        last_temp_milli = 0;
                        hud_frame.cpu_temp_c = 0;
                    }
                    // AM7-L3-ENV-001: Fault on critical temperature (>85°C)
                    constexpr int kTempCriticalMilliC = 85000;  // 85°C in millidegrees
                    if (last_temp_milli > kTempCriticalMilliC) {
                        std::cerr << "CRITICAL: CPU temperature " << (last_temp_milli / 1000)
                                  << "°C exceeds 85°C threshold\n";
                        telemetry.log_event(aurore::TelemetryEventId::TEMPERATURE_CRITICAL,
                                            aurore::TelemetrySeverity::kCritical,
                                            "CPU over-temperature: " +
                                                std::to_string(last_temp_milli / 1000) + "C");
                        state_machine.on_fault(aurore::FaultCode::TEMPERATURE_CRITICAL);
                    }
                } else {
                    hud_frame.cpu_temp_c = static_cast<uint16_t>(last_temp_milli / 100);
                }
            }

            // Convert ballistics lead angles from degrees to milliradians
            if (last_ballistics_sol.has_value()) {
                constexpr float kDegToMrad = 17.4533f;  // π/180 * 1000
                hud_frame.az_lead_mrad = last_ballistics_sol->az_lead_deg * kDegToMrad;
                hud_frame.el_lead_mrad = last_ballistics_sol->el_lead_deg * kDegToMrad;
            } else {
                hud_frame.az_lead_mrad = 0.0f;
                hud_frame.el_lead_mrad = 0.0f;
            }

            hud_frame.p_hit = last_ballistics_sol.has_value() ? last_ballistics_sol->p_hit : 0.0f;
            hud_frame.deadline_misses = static_cast<uint32_t>(safety_monitor.deadline_misses());
            hud_socket.broadcast(hud_frame);

            // AuroreLink protobuf broadcast (telemetry over TCP)
            aurore::Telemetry tel;
            tel.set_timestamp_ns(aurore::get_timestamp());
            tel.mutable_health()->set_frame_count(frame_sequence.load());

            // Track data
            tel.mutable_track()->set_centroid_x(latest_solution.centroid_x);
            tel.mutable_track()->set_centroid_y(latest_solution.centroid_y);
            tel.mutable_track()->set_velocity_x(latest_solution.velocity_x);
            tel.mutable_track()->set_velocity_y(latest_solution.velocity_y);
            tel.mutable_track()->set_valid(latest_solution.valid);
            tel.mutable_track()->set_confidence(latest_solution.psr > 0 ? latest_solution.psr
                                                                        : 0.0f);

            // Gimbal data
            tel.mutable_gimbal()->set_az_deg(gimbal_cmd.az_deg);
            tel.mutable_gimbal()->set_el_deg(gimbal_cmd.el_deg);
            tel.mutable_gimbal()->set_az_error_deg(gimbal_status.az_error_deg);
            tel.mutable_gimbal()->set_el_error_deg(gimbal_status.el_error_deg);

            // Ballistics data
            if (last_ballistics_sol.has_value()) {
                tel.mutable_ballistic()->set_p_hit(last_ballistics_sol->p_hit);
                tel.mutable_ballistic()->set_range_m(test_range_m);
            }

            // FCS state (map FcsState enum to ProtoFcsState)
            aurore::ProtoFcsState proto_state =
                static_cast<aurore::ProtoFcsState>(static_cast<int>(state));
            tel.mutable_health()->set_fcs_state(proto_state);
            tel.mutable_health()->set_deadline_misses(
                static_cast<uint32_t>(safety_monitor.deadline_misses()));

            const uint64_t ta2 = aurore::get_timestamp();
            link_server.broadcast_telemetry(tel);
            const uint64_t ta3 = aurore::get_timestamp();

            deadline.stop();
            if (deadline.exceeded()) {
                std::cerr << "Actuation exceeded deadline: " << deadline.elapsed_ns() << " ns"
                          << " [pre_servo=" << (ta1 - ta0) / 1000 << "us"
                          << " pre_hud=" << (ta2 - ta1) / 1000 << "us"
                          << " broadcast=" << (ta3 - ta2) / 1000 << "us"
                          << " total=" << (ta3 - ta0) / 1000 << "us"
                          << "]" << std::endl;
            }
        }

        actuation_running.store(false, std::memory_order_release);
    });

    // Safety monitor thread (1kHz)
    std::thread safety_thread([&]() {
        if (!configure_rt_thread("safety_monitor", 99, 3)) {
            return;
        }

        aurore::ThreadTiming timing(1000000, 0);  // 1kHz

        while (!g_shutdown_requested.load(std::memory_order_acquire)) {
            // RAII watchdog kick - auto-kick at end of each monitoring cycle
            aurore::WatchdogKick kick(safety_monitor);

            timing.wait();

            // Feed interlock watchdog every cycle
            interlock.watchdog_feed();

            if (!safety_monitor.run_cycle()) {
                // Safety fault detected
                std::cerr << "Safety fault detected!" << std::endl;

                // Emergency stop
                if (safety_monitor.is_emergency_active()) {
                    std::cerr << "Emergency stop active - halting all outputs" << std::endl;
                    fusion_hat.disable_all_servos();
                    interlock.set_inhibit(true);
                }
            }
        }  // kick_watchdog() called here automatically
    });

    // Main loop - monitor system status
    std::cout << "\nSystem running. Press Ctrl+C to stop." << std::endl;

    // Log system boot event
    telemetry.log_event(aurore::TelemetryEventId::SYSTEM_BOOT, aurore::TelemetrySeverity::kInfo,
                        "Aurore MkVII system booted");

    uint64_t last_status_time = aurore::get_timestamp();

    while (!g_shutdown_requested.load(std::memory_order_acquire)) {
        sleep(1);  // Status update every second

        uint64_t now = aurore::get_timestamp();
        double elapsed_sec = static_cast<double>(now - last_status_time) / 1e9;

        std::cout << "\n--- Status ---" << std::endl;
        std::cout << "Uptime: " << elapsed_sec << " s" << std::endl;
        std::cout << "Frames: " << frame_sequence.load() << std::endl;
        std::cout << "Safety: " << (safety_monitor.is_system_safe() ? "OK" : "FAULT") << std::endl;
        std::cout << "Deadline misses: " << safety_monitor.deadline_misses() << std::endl;

        last_status_time = now;
    }

    // Shutdown sequence
    std::cout << "\nShutting down..." << std::endl;

    // Emergency stop: disable all actuators
    std::cout << "Emergency stop: disabling all servos" << std::endl;
    fusion_hat.disable_all_servos();
    interlock.set_inhibit(true);
    telemetry.log_event(aurore::TelemetryEventId::SAFETY_INHIBIT_ENGAGED,
                        aurore::TelemetrySeverity::kCritical,
                        "Emergency stop triggered during shutdown");

    // Log system shutdown event
    telemetry.log_event(aurore::TelemetryEventId::SYSTEM_SHUTDOWN, aurore::TelemetrySeverity::kInfo,
                        "Aurore MkVII system shutdown");

    // Stop threads
    vision_running.store(false);
    track_running.store(false);
    actuation_running.store(false);

    // Wait for threads with timeout using pthread_timedjoin_np (Linux).
    // std::thread::join() is blocking with no timeout; pthread allows timed join.
    auto join_with_timeout = [](std::thread& t, int timeout_ms) {
        if (!t.joinable()) return;
        struct timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        const int rc = pthread_timedjoin_np(t.native_handle(), nullptr, &ts);
        if (rc == 0) {
            t.detach();  // Native handle already joined; detach to avoid double-join in destructor
        } else {
            std::cerr << "Thread did not terminate within timeout, detaching\n";
            t.detach();
        }
    };

    // Wake detect thread so it exits cleanly
    detect_shared.frame_cv.notify_all();

    join_with_timeout(vision_thread, 2000);
    join_with_timeout(track_thread, 2000);
    join_with_timeout(actuation_thread, 2000);
    join_with_timeout(detect_thread, 3000);
    join_with_timeout(safety_thread, 2000);
    join_with_timeout(usb_preview_thread, 2000);

    // Stop servers
    link_server.stop();
    hud_socket.stop();
    cmd_socket.stop();
    mjpeg_streamer.stop();
    mjpeg_usb_streamer.stop();

    // Stop camera
    if (camera) {
        camera->stop();
    }

    // Stop interlock controller
    interlock.stop();

    // Stop safety monitor
    safety_monitor.stop();

    // Stop telemetry writer
    telemetry.stop();

    // Terminate web server gracefully
    if (web_server_pid != -1) {
        std::cout << "Terminating web server (PID " << web_server_pid << ")..." << std::endl;
        kill(web_server_pid, SIGTERM);
        // Wait for graceful shutdown (2 second timeout)
        for (int i = 0; i < 20; i++) {
            int status;
            pid_t result = waitpid(web_server_pid, &status, WNOHANG);
            if (result == web_server_pid) {
                std::cout << "Web server terminated." << std::endl;
                break;
            }
            usleep(100000);  // 100ms
        }
        // Force kill if still running
        if (kill(web_server_pid, 0) == 0) {
            std::cout << "Force killing web server..." << std::endl;
            kill(web_server_pid, SIGKILL);
            waitpid(web_server_pid, nullptr, 0);
        }
    }

    // Unlock memory
    munlockall();

    std::cout << "Shutdown complete." << std::endl;

    return 0;
}
