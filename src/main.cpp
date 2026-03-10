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

#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
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
#include "aurore/config_loader.hpp"
#include "aurore/detector.hpp"  // For OrbDetector
#include "aurore/fusion_hat.hpp"
#include "aurore/gimbal_controller.hpp"
#include "aurore/hud_socket.hpp"
#include "aurore/interlock_controller.hpp"
#include "aurore/ring_buffer.hpp"
#include "aurore/safety_monitor.hpp"
#include "aurore/state_machine.hpp"  // For TrackSolution
#include "aurore/telemetry_writer.hpp"
#include "aurore/timing.hpp"
#include "aurore/tracker.hpp"  // For KcfTracker

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
        std::cerr << "Warning: Failed to lock memory: " << strerror(errno) << std::endl;
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
    set_resource_limits();

    // Lock memory
    lock_memory();

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
    safety_config.actuation_deadline_ns =
        static_cast<uint64_t>(config.get_int("safety.actuation_deadline_ns", 2000000));
    safety_config.max_consecutive_misses =
        static_cast<size_t>(config.get_int("safety.max_consecutive_misses", 3));

    if (dry_run) {
        // Relaxed deadlines for non-RT laptop
        safety_config.vision_deadline_ns = 1000000000;     // 1s
        safety_config.actuation_deadline_ns = 1000000000;  // 1s
        safety_config.max_consecutive_misses = 100;
    }

    aurore::SafetyMonitor safety_monitor(safety_config);

    if (!dry_run) {
        safety_monitor.init();
    }

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
            std::cerr << "Warning: Failed to drop privileges, continuing as root" << std::endl;
            // Continue anyway - this is a warning, not a fatal error
        }
    }

    // Initialize camera (if not dry-run)
    std::unique_ptr<aurore::CameraWrapper> camera;

    // Initialize camera (test pattern in dry-run, real camera otherwise)
    try {
        aurore::CameraConfig cam_config;
        cam_config.width = aurore::DEFAULT_WIDTH;
        cam_config.height = aurore::DEFAULT_HEIGHT;
        cam_config.fps = aurore::DEFAULT_FPS;

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
    aurore::AuroreLinkServer link_server(link_cfg);
    link_server.start();

    // Initialize HUD socket for low-latency telemetry to aurore-link frontend
    aurore::HudSocketConfig hud_cfg;
    hud_cfg.socket_path =
        config.get_string("network.hud_telemetry.socket_path", "/run/aurore/hud_telemetry.sock");
    // In dry-run, socket path should be writable; in production, requires root
    hud_cfg.require_root_uid = !dry_run;
    aurore::HudSocket hud_socket(hud_cfg);
    if (hud_socket.start()) {
        std::cout << "HUD socket listening: " << hud_cfg.socket_path << std::endl;
    } else {
        std::cerr << "Warning: HUD socket failed to start" << std::endl;
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
    fusion_hat.set_endstop_limits(0, az_min, az_max);  // Channel 0 = azimuth
    fusion_hat.set_endstop_limits(1, el_min, el_max);  // Channel 1 = elevation

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

    // Frame ring buffer (zero-copy)
    aurore::LockFreeRingBuffer<aurore::ZeroCopyFrame, 4> frame_buffer;

    // INT-003 Fix: Track solution ring buffer (track_compute -> actuation_output)
    aurore::LockFreeRingBuffer<aurore::TrackSolution, 4> track_buffer;

    // Instantiate ORB detector for target detection
    aurore::OrbDetector detector;
    const std::string descriptor_path =
        config.get_string("detector.descriptor_path", "target_signatures/helicopter.yml");
    if (!detector.load_descriptor_file(descriptor_path)) {
        // Descriptor file failed to load
        if (dry_run) {
            // In dry-run mode, synthesize a test template so detector is ready
            cv::Mat test_template = cv::Mat::zeros(80, 80, CV_8UC3);
            cv::rectangle(test_template, {10, 10, 60, 60}, {0, 200, 100}, -1);
            cv::putText(test_template, "T", {25, 50}, cv::FONT_HERSHEY_SIMPLEX, 2.0, {255, 255, 0},
                        3);
            detector.add_template(test_template);
            std::cout << "Detector: Using synthesized test template (dry-run mode)" << std::endl;
        } else {
            std::cerr << "Warning: Failed to load descriptor file: " << descriptor_path
                      << std::endl;
        }
    } else {
        std::cout << "Detector: Loaded descriptors from " << descriptor_path << std::endl;
    }

    // State machine for FCS mode management
    aurore::StateMachine state_machine;

    // Set state change callback for telemetry and stdout logging
    state_machine.set_state_change_callback(
        [&telemetry](aurore::FcsState from, aurore::FcsState to) {
            std::cout << "State: " << aurore::fcs_state_name(from) << " -> "
                      << aurore::fcs_state_name(to) << std::endl;
            if (to == aurore::FcsState::FAULT) {
                telemetry.log_event(aurore::TelemetryEventId::SAFETY_FAULT,
                                    aurore::TelemetrySeverity::kCritical,
                                    "State machine transitioned to FAULT");
            }
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

    // Install arm callback for operator authorization
    link_server.set_arm_callback([&](bool authorized) {
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

    // Control loop state
    std::atomic<uint64_t> frame_sequence(0);
    std::atomic<bool> vision_running(false);
    std::atomic<bool> track_running(false);
    std::atomic<bool> actuation_running(false);
    std::atomic<uint64_t> last_track_sequence(0);

    // Signal hardware init complete (BOOT -> IDLE_SAFE)
    state_machine.on_init_complete();

    // Vision pipeline thread
    std::thread vision_thread([&]() {
        if (!configure_rt_thread("vision_pipeline", 90, 2)) {
            return;
        }

        aurore::ThreadTiming timing(8333333, 0);    // 120Hz, no phase offset
        aurore::DeadlineMonitor deadline(3000000);  // 3ms budget

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
                    if (!frame_buffer.push(frame)) {
                        // Buffer full - frame dropped
                    }

                    // Update safety monitor
                    safety_monitor.update_vision_frame(frame.sequence, frame.timestamp_ns);
                }

                deadline.stop();
                if (deadline.exceeded()) {
                    std::cerr << "Vision processing exceeded deadline: " << deadline.elapsed_ns()
                              << " ns" << std::endl;
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
        aurore::DeadlineMonitor deadline(2000000);      // 2ms budget

        track_running.store(true, std::memory_order_release);

        // INT-003 Fix: Track solution for actuation output
        aurore::TrackSolution current_solution;
        current_solution.valid = false;

        // Vision pipeline integration: KCF tracker instance
        aurore::KcfTracker tracker;
        tracker.set_camera(camera.get());  // Zero-copy: tracker holds DMA buffer references

        // Vision watchdog: track last frame timestamp
        uint64_t last_frame_ns = aurore::get_timestamp();
        constexpr uint64_t kVisionWatchdogNs = 10000000;  // 10ms timeout

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

                // Wrap frame as OpenCV Mat (RAW10→BGR888 conversion happens here)
                cv::Mat bgr_frame = camera->wrap_as_mat(frame, aurore::PixelFormat::BGR888);

                if (!bgr_frame.empty()) {
                    aurore::FcsState state = state_machine.state();

                    if (state == aurore::FcsState::SEARCH) {
                        // SEARCH mode: use ORB detector to find target
                        auto detection = detector.detect(bgr_frame);
                        if (detection.has_value()) {
                            // Target found - initialize tracker
                            cv::Rect2d det_bbox(static_cast<float>(detection->bbox.x),
                                                static_cast<float>(detection->bbox.y),
                                                static_cast<float>(detection->bbox.w),
                                                static_cast<float>(detection->bbox.h));
                            if (tracker.init(bgr_frame, det_bbox)) {
                                // AM7-L3-VIS-001: Zero-copy reference template capture
                                // Pass ZeroCopyFrame descriptor, NOT the wrapped cv::Mat
                                tracker.capture_reference_template(frame, det_bbox);
                                // Signal detection to state machine with ORB confidence
                                state_machine.on_detection(*detection);
                            }
                        }
                        // No detection - stay in SEARCH
                        current_solution.valid = false;
                        current_solution.centroid_x = static_cast<float>(frame.width) / 2.0f;
                        current_solution.centroid_y = static_cast<float>(frame.height) / 2.0f;
                    } else if (state == aurore::FcsState::TRACKING ||
                               state == aurore::FcsState::ARMED) {
                        // TRACKING/ARMED: update KCF tracker
                        current_solution = tracker.update(bgr_frame);

                        if (current_solution.valid) {
                            // Validate solution bounds
                            if (current_solution.centroid_x < 0 ||
                                current_solution.centroid_x > static_cast<float>(frame.width) ||
                                current_solution.centroid_y < 0 ||
                                current_solution.centroid_y > static_cast<float>(frame.height)) {
                                current_solution.valid = false;
                                tracker.reset();
                            } else {
                                // Valid tracking solution - use fixed confidence 0.75f to avoid KCF
                                // PSR issues
                                current_solution.psr = 0.75f;
                                state_machine.on_tracker_update(current_solution);
                            }
                        }

                        if (!current_solution.valid) {
                            // Track lost - attempt redetection
                            float redetect_score = tracker.redetect(bgr_frame);
                            state_machine.on_redetection_score(redetect_score);
                            if (redetect_score < 0.85f) {
                                // Redetection failed - reset tracker and fall back to SEARCH
                                tracker.reset();
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

                // Push solution to actuation buffer
                if (!track_buffer.push(current_solution)) {
                    // Buffer full - solution dropped
                }
                last_track_sequence.store(frame.sequence, std::memory_order_release);

                // Zero-copy release
                camera->release_frame(frame);

                deadline.stop();
                if (deadline.exceeded()) {
                    std::cerr << "Track compute exceeded deadline: " << deadline.elapsed_ns()
                              << " ns" << std::endl;
                }
            } else {
                // No frame available - check vision watchdog
                uint64_t elapsed = now_ns - last_frame_ns;
                if (elapsed > kVisionWatchdogNs && frame_available) {
                    // Vision timeout detected
                    state_machine.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
                    telemetry.log_event(aurore::TelemetryEventId::CAMERA_TIMEOUT,
                                        aurore::TelemetrySeverity::kWarning,
                                        "Vision pipeline timeout (>10ms)");
                }

                // Output invalid solution
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

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            // RAII watchdog kick - auto-kick at end of each loop iteration
            aurore::WatchdogKick kick(safety_monitor);

            timing.wait();
            deadline.start();

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
            if (actuation_allowed &&
                (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED ||
                 state == aurore::FcsState::FREECAM)) {
                fusion_hat.set_servo_angle(0, gimbal_cmd.az_deg);  // ch0 = azimuth
                fusion_hat.set_servo_angle(1, gimbal_cmd.el_deg);  // ch1 = elevation
            }

            // Check I2C error threshold and trigger fault if exceeded
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

            // Compute ballistics solution if tracking
            if (latest_solution.valid && state == aurore::FcsState::TRACKING) {
                // Estimate target aspect angle (elevation from gimbal + range offset)
                const float target_aspect = gimbal_cmd.el_deg;
                last_ballistics_sol = ballistics.solve(
                    test_range_m,          // Use configured test range for dry-run
                    gimbal_cmd.el_deg,     // Current gimbal elevation
                    target_aspect,         // Target aspect (equals gimbal el in simplified model)
                    muzzle_velocity_mps);  // Configured muzzle velocity

                if (last_ballistics_sol.has_value()) {
                    // Signal ballistics solution to state machine (enables ARMED mode)
                    state_machine.on_ballistics_solution(*last_ballistics_sol);
                }
            }

            // Read gimbal status from actual servo feedback
            aurore::GimbalStatusSm gimbal_status;
            if (auto az = fusion_hat.get_servo_angle(0)) {
                gimbal_status.az_error_deg = std::abs(*az - gimbal_cmd.az_deg);
            }
            if (auto el = fusion_hat.get_servo_angle(1)) {
                gimbal_status.el_error_deg = std::abs(*el - gimbal_cmd.el_deg);
            }
            gimbal_status.velocity_deg_s = 0.0f;  // TODO: Read from gimbal
            state_machine.on_gimbal_status(gimbal_status);

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
            hud_frame.target_w = latest_solution.valid ? 50.0f : 0.0f;  // Placeholder bbox size
            hud_frame.target_h = latest_solution.valid ? 50.0f : 0.0f;
            hud_frame.velocity_x = latest_solution.velocity_x;
            hud_frame.velocity_y = latest_solution.velocity_y;
            hud_frame.confidence = latest_solution.psr > 0 ? latest_solution.psr : 0.0f;
            hud_frame.range_m = test_range_m;
            hud_frame.timestamp_ns = aurore::get_timestamp();

            // SYSTEM_STATUS fields (AM7-L2-HUD-004)
            hud_frame.interlock = interlock.is_actuation_allowed() ? 1 : 0;
            hud_frame.target_lock =
                (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED) ? 1 : 0;
            hud_frame.fault_active = (state == aurore::FcsState::FAULT) ? 1 : 0;
            // CPU temp: read from /sys/class/thermal/thermal_zone0/temp (millidegrees C)
            {
                std::ifstream thermal_file("/sys/class/thermal/thermal_zone0/temp");
                int temp_milli = 0;
                if (thermal_file >> temp_milli) {
                    hud_frame.cpu_temp_c = static_cast<uint16_t>(temp_milli / 100);  // ×10
                } else {
                    hud_frame.cpu_temp_c = 0;
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

            link_server.broadcast_telemetry(tel);

            deadline.stop();
            if (deadline.exceeded()) {
                std::cerr << "Actuation exceeded deadline: " << deadline.elapsed_ns() << " ns"
                          << std::endl;
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

    // Wait for threads with timeout
    auto join_with_timeout = [](std::thread& t, int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (t.joinable()) {
                // Try to join with a short wait
                t.join();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (t.joinable()) {
            std::cerr << "Thread did not terminate, detaching" << std::endl;
            t.detach();
        }
    };

    join_with_timeout(vision_thread, 2000);
    join_with_timeout(track_thread, 2000);
    join_with_timeout(actuation_thread, 2000);
    join_with_timeout(safety_thread, 2000);

    // Stop servers
    link_server.stop();
    hud_socket.stop();

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

    // Unlock memory
    munlockall();

    std::cout << "Shutdown complete." << std::endl;

    return 0;
}
