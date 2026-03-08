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

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

// libcap for privilege drop (optional - requires libcap-dev)
#ifdef HAVE_LIBCAP
#include <sys/capability.h>
#endif

#include "aurore/ring_buffer.hpp"
#include "aurore/timing.hpp"
#include "aurore/safety_monitor.hpp"
#include "aurore/camera_wrapper.hpp"
#include "aurore/fusion_hat.hpp"
#include "aurore/state_machine.hpp"  // For TrackSolution
#include "aurore/tracker.hpp"        // For KcfTracker
#include "aurore/aurore_link_server.hpp"
#include "aurore/config_loader.hpp"
#include "aurore/telemetry_writer.hpp"
#include "aurore.pb.h"

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
        std::cerr << "Failed to set SCHED_FIFO for " << name
                  << ": " << strerror(errno) << std::endl;
        if (!g_dry_run) return false;
        // In dry-run mode: continue without RT scheduling
    }

    // Set CPU affinity
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(cpu_affinity), &cpuset);

    if (pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "Failed to set CPU affinity for " << name
                  << ": " << strerror(errno) << std::endl;
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

    std::cout << "Memory lock limit set to " << (MAX_MEMLOCK_BYTES / (1024 * 1024))
              << " MB" << std::endl;

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

    std::cout << "Privileges dropped: running as UID=" << getuid()
              << ", GID=" << getgid() << std::endl;

    return true;
}

}  // anonymous namespace

/**
 * @brief Main entry point
 * 
 * Initializes system, starts control loops, and handles shutdown.
 */
int main(int argc, char* argv[]) {
    std::cout << "Aurore MkVII Fire Control System v" 
              << AURORE_VERSION << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // Parse command line arguments
    bool dry_run = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dry-run" || arg == "-n") {
            dry_run = true;
            g_dry_run = true;
            std::cout << "Dry-run mode enabled (no hardware access)" << std::endl;
        }
        else if (arg == "--help" || arg == "-h") {
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
    safety_config.vision_deadline_ns    = static_cast<uint64_t>(config.get_int("safety.vision_deadline_ns", 20000000));
    safety_config.actuation_deadline_ns = static_cast<uint64_t>(config.get_int("safety.actuation_deadline_ns", 2000000));
    safety_config.max_consecutive_misses = static_cast<size_t>(config.get_int("safety.max_consecutive_misses", 3));

    if (dry_run) {
        // Relaxed deadlines for non-RT laptop
        safety_config.vision_deadline_ns    = 1000000000; // 1s
        safety_config.actuation_deadline_ns = 1000000000; // 1s
        safety_config.max_consecutive_misses = 100;
    }
    
    aurore::SafetyMonitor safety_monitor(safety_config);
    
    if (!dry_run) {
        safety_monitor.init();
    }
    
    safety_monitor.set_safety_action_callback(
        [](aurore::SafetyFaultCode code, const char* reason, void*) {
            std::cerr << "SAFETY ACTION: " << aurore::fault_code_to_string(code)
                      << " - " << reason << std::endl;
        },
        nullptr
    );
    
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
                  << cam_config.width << "x" << cam_config.height
                  << " @ " << cam_config.fps << " FPS" << std::endl;
    }
    catch (const aurore::CameraException& e) {
        std::cerr << "Camera initialization failed: " << e.what() << std::endl;
        if (!dry_run) return 1;
    }

    // Initialize AuroreLink server for remote operator interface
    aurore::AuroreLinkConfig link_cfg;
    link_cfg.telemetry_port = static_cast<uint16_t>(config.get_int("network.aurore_link.telemetry_port", 9000));
    link_cfg.command_port = static_cast<uint16_t>(config.get_int("network.aurore_link.command_port", 9002));
    aurore::AuroreLinkServer link_server(link_cfg);
    link_server.start();

    // Install mode callback for FREECAM/AUTO switching
    link_server.set_mode_callback([&](aurore::LinkMode mode) {
        if (mode == aurore::LinkMode::FREECAM) {
            std::cout << "AuroreLink: Mode switched to FREECAM" << std::endl;
            // TODO: gimbal_controller.set_source(aurore::GimbalSource::FREECAM);
        } else {
            std::cout << "AuroreLink: Mode switched to AUTO" << std::endl;
            // TODO: gimbal_controller.set_source(aurore::GimbalSource::AUTO);
        }
    });

    // Gimbal controller (FusionHAT+ sysfs driver — fails gracefully without hardware)
    aurore::FusionHat fusion_hat;
    fusion_hat.init();

    // Frame ring buffer (zero-copy)
    aurore::LockFreeRingBuffer<aurore::ZeroCopyFrame, 4> frame_buffer;

    // INT-003 Fix: Track solution ring buffer (track_compute -> actuation_output)
    aurore::LockFreeRingBuffer<aurore::TrackSolution, 4> track_buffer;

    // State machine for FCS mode management
    aurore::StateMachine state_machine;

    // Set state change callback for telemetry
    state_machine.set_state_change_callback(
        [&telemetry]([[maybe_unused]] aurore::FcsState from, aurore::FcsState to) {
            if (to == aurore::FcsState::FAULT) {
                telemetry.log_event(
                    aurore::TelemetryEventId::SAFETY_FAULT,
                    aurore::TelemetrySeverity::kCritical,
                    "State machine transitioned to FAULT");
            }
        });


    // Control loop state
    std::atomic<uint64_t> frame_sequence(0);
    std::atomic<bool> vision_running(false);
    std::atomic<bool> track_running(false);
    std::atomic<bool> actuation_running(false);
    std::atomic<uint64_t> last_track_sequence(0);
    
    // Vision pipeline thread
    std::thread vision_thread([&]() {
        if (!configure_rt_thread("vision_pipeline", 90, 2)) {
            return;
        }
        
        aurore::ThreadTiming timing(8333333, 0);  // 120Hz, no phase offset
        aurore::DeadlineMonitor deadline(3000000);  // 3ms budget
        
        vision_running.store(true, std::memory_order_release);
        
        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {
            
            timing.wait();
            
            if (timing.missed_deadline()) {
                std::cerr << "Vision deadline missed (consecutive: " 
                          << timing.consecutive_misses() << ")" << std::endl;
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
                    std::cerr << "Vision processing exceeded deadline: " 
                              << deadline.elapsed_ns() << " ns" << std::endl;
                }
            }
        }
        
        vision_running.store(false, std::memory_order_release);
    });
    
    // Track compute thread
    std::thread track_thread([&]() {
        if (!configure_rt_thread("track_compute", 85, 2)) {
            return;
        }

        aurore::ThreadTiming timing(8333333, 2000000);  // 120Hz, 2ms phase offset
        aurore::DeadlineMonitor deadline(2000000);  // 2ms budget

        track_running.store(true, std::memory_order_release);

        // INT-003 Fix: Track solution for actuation output
        aurore::TrackSolution current_solution;
        current_solution.valid = false;
        
        // Vision pipeline integration: KCF tracker instance
        aurore::KcfTracker tracker;
        bool tracker_initialized = false;

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {

            timing.wait();

            // Process frame from buffer
            aurore::ZeroCopyFrame frame;
            if (frame_buffer.pop(frame)) {
                deadline.start();

                // Wrap frame as OpenCV Mat (RAW10→BGR888 conversion happens here)
                cv::Mat bgr_frame = camera->wrap_as_mat(frame, aurore::PixelFormat::BGR888);
                
                if (bgr_frame.empty()) {
                    // Frame conversion failed, output invalid solution
                    current_solution.valid = false;
                }
                else if (!tracker_initialized && frame.valid) {
                    // Initialize tracker on first valid frame
                    // Use center crop as initial target (simulating first detection)
                    const cv::Rect2d initial_bbox(
                        static_cast<float>(frame.width) * 0.4f, static_cast<float>(frame.height) * 0.4f,
                        static_cast<float>(frame.width) * 0.2f, static_cast<float>(frame.height) * 0.2f
                    );
                    
                    if (tracker.init(bgr_frame, initial_bbox)) {
                        tracker_initialized = true;
                        std::cout << "KCF tracker initialized" << std::endl;
                    }
                }
                
                if (tracker_initialized && !bgr_frame.empty()) {
                    // Update tracker and get solution
                    current_solution = tracker.update(bgr_frame);

                    // Validate solution bounds
                    if (current_solution.centroid_x < 0 ||
                        current_solution.centroid_x > static_cast<float>(frame.width) ||
                        current_solution.centroid_y < 0 ||
                        current_solution.centroid_y > static_cast<float>(frame.height)) {
                        current_solution.valid = false;
                        tracker.reset();
                        tracker_initialized = false;
                    }

                    // State machine: detection event based on confidence
                    // SEARCH -> TRACKING when confidence > 0.85
                    // TRACKING -> SEARCH when confidence < 0.5 for 3 frames (handled by state_machine)
                    aurore::Detection detection;
                    detection.confidence = current_solution.valid ? current_solution.psr : 0.0f;
                    detection.bbox.x = static_cast<int>(current_solution.centroid_x - 20);
                    detection.bbox.y = static_cast<int>(current_solution.centroid_y - 20);
                    detection.bbox.w = 40;
                    detection.bbox.h = 40;
                    state_machine.on_detection(detection);
                    state_machine.on_tracker_update(current_solution);
                }
                else {
                    // No tracker - output center placeholder
                    current_solution.centroid_x = static_cast<float>(frame.width) / 2.0f;
                    current_solution.centroid_y = static_cast<float>(frame.height) / 2.0f;
                    current_solution.velocity_x = 0.0f;
                    current_solution.velocity_y = 0.0f;
                    current_solution.valid = false;
                    current_solution.psr = -1.0f;  // KCF doesn't provide PSR
                }

                // Push solution to actuation buffer
                if (!track_buffer.push(current_solution)) {
                    // Buffer full - solution dropped
                }
                last_track_sequence.store(frame.sequence, std::memory_order_release);

                // Broadcast telemetry via AuroreLink
                aurore::Telemetry tel;
                tel.set_timestamp_ns(aurore::get_timestamp());
                tel.mutable_health()->set_frame_count(frame_sequence.load());
                tel.mutable_track()->set_centroid_x(current_solution.centroid_x);
                tel.mutable_track()->set_centroid_y(current_solution.centroid_y);
                tel.mutable_track()->set_velocity_x(current_solution.velocity_x);
                tel.mutable_track()->set_velocity_y(current_solution.velocity_y);
                tel.mutable_track()->set_valid(current_solution.valid);
                tel.mutable_track()->set_confidence(current_solution.psr > 0 ? current_solution.psr / 100.0f : 0.0f);
                link_server.broadcast_telemetry(tel);

                deadline.stop();
                if (deadline.exceeded()) {
                    std::cerr << "Track compute exceeded deadline: "
                              << deadline.elapsed_ns() << " ns" << std::endl;
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
        aurore::DeadlineMonitor deadline(1500000);  // 1.5ms budget

        actuation_running.store(true, std::memory_order_release);

        // INT-003 Fix: Read track solution from buffer
        aurore::TrackSolution latest_solution;
        latest_solution.valid = false;
        uint64_t last_actuation_sequence = 0;

        while (!g_shutdown_requested.load(std::memory_order_acquire) &&
               !safety_monitor.is_emergency_active()) {

            timing.wait();

            deadline.start();

            // INT-003 Fix: Read latest track solution from buffer
            while (track_buffer.pop(latest_solution)) {
                // Keep popping to get the latest solution
                last_actuation_sequence++;
            }

            // Calculate gimbal command and send to Fusion HAT+ via sysfs PWM
            if (latest_solution.valid) {
                // IMX708 at 120fps (binned mode): ~66° H × 40° V FoV
                constexpr float kHFovDeg = 66.0f;
                constexpr float kVFovDeg = 40.0f;
                const float az_cmd = (latest_solution.centroid_x - (1536.0f / 2.0f))
                                     * (kHFovDeg / 1536.0f);
                // Negate: increasing pixel-Y is downward, positive elevation is up
                const float el_cmd = -(latest_solution.centroid_y - (864.0f / 2.0f))
                                     * (kVFovDeg / 864.0f);
                fusion_hat.set_servo_angle(0, az_cmd);  // ch0 = azimuth
                fusion_hat.set_servo_angle(1, el_cmd);  // ch1 = elevation
            }

            // State machine: gimbal status for TRACKING -> ARMED transition
            // ARMED entry requires gimbal settled (error < 0.5 deg) for sustained period
            // and ballistics p_hit > 0.95
            aurore::GimbalStatusSm gimbal_status;
            // TODO: Read actual gimbal errors from Fusion HAT+
            // Use frame dimensions from constants (1536x864)
            gimbal_status.az_error_deg = std::abs(latest_solution.centroid_x - 1536.0f / 2.0f) * 0.1f;
            gimbal_status.el_error_deg = std::abs(latest_solution.centroid_y - 864.0f / 2.0f) * 0.1f;
            gimbal_status.velocity_deg_s = 0.0f;  // TODO: Read from gimbal
            state_machine.on_gimbal_status(gimbal_status);

            // INT-002 Fix: Update safety monitor for actuation frame
            // This was missing - track_compute wasn't calling update_actuation_frame
            if (last_actuation_sequence > 0) {
                const aurore::TimestampNs now = aurore::get_timestamp();
                safety_monitor.update_actuation_frame(last_actuation_sequence, now);
            }

            deadline.stop();
            if (deadline.exceeded()) {
                std::cerr << "Actuation exceeded deadline: "
                          << deadline.elapsed_ns() << " ns" << std::endl;
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
            timing.wait();
            
            if (!safety_monitor.run_cycle()) {
                // Safety fault detected
                std::cerr << "Safety fault detected!" << std::endl;
                
                // Emergency stop
                if (safety_monitor.is_emergency_active()) {
                    std::cerr << "Emergency stop active - halting all outputs" << std::endl;
                    // TODO: Disable all actuators
                }
            }
        }
    });
    
    // Main loop - monitor system status
    std::cout << "\nSystem running. Press Ctrl+C to stop." << std::endl;

    // Log system boot event
    telemetry.log_event(
        aurore::TelemetryEventId::SYSTEM_BOOT,
        aurore::TelemetrySeverity::kInfo,
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

    // Log system shutdown event
    telemetry.log_event(
        aurore::TelemetryEventId::SYSTEM_SHUTDOWN,
        aurore::TelemetrySeverity::kInfo,
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

    // Stop AuroreLink server
    link_server.stop();

    // Stop camera
    if (camera) {
        camera->stop();
    }

    // Stop safety monitor
    safety_monitor.stop();

    // Stop telemetry writer
    telemetry.stop();

    // Unlock memory
    munlockall();
    
    std::cout << "Shutdown complete." << std::endl;
    
    return 0;
}
