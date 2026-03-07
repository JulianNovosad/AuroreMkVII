// Verified headers: [monitor.h, application.h, util_logging.h, orientation_sensor.h, iostream...]
// Verification timestamp: 2026-01-06 17:08:04
#include "monitor.h"
#include "../application.h"  // Include the full definition
#include "camera/camera_capture.h"
#include "inference/inference.h"
#include "util_logging.h"
#include "orientation_sensor.h"  // Include orientation sensor header for OrientationData
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <random>
#include <future>

Monitor::Monitor(Application& app) : app_(app), running_(false) {
    // Generate a unique run ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    run_id_ = "RUN_" + std::to_string(dis(gen)) + "_" + 
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Initialize run start time
    run_start_time_ = std::chrono::steady_clock::now();
}

Monitor::~Monitor() {
    stop();
}

void Monitor::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }
    
    monitor_thread_ = std::thread(&Monitor::monitor_thread_func, this);
}

void Monitor::stop() {
    if (running_.exchange(false)) {
        if (monitor_thread_.joinable()) {
            auto shared_promise = std::make_shared<std::promise<void>>();
            std::future<void> future = shared_promise->get_future();
            
            std::thread joiner_thread([this, shared_promise]() {
                try {
                    if (monitor_thread_.joinable()) {
                        monitor_thread_.join();
                    }
                    shared_promise->set_value();
                } catch (...) {}
            });
            
            if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
                APP_LOG_WARNING("[SHUTDOWN] Monitor thread did not join within 3s, detaching.");
                if (monitor_thread_.joinable()) {
                    monitor_thread_.detach();
                }
                joiner_thread.detach();
            } else {
                if (joiner_thread.joinable()) {
                    joiner_thread.join();
                }
            }
        }
    }
}

void Monitor::monitor_thread_func() {
    // Give the application some time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    while (running_) {
        // Clear screen and move cursor to top-left
        std::cout << "\033[2J\033[1;1H";
        
        // Print header with run ID and relative time
        std::cout << "==========================================\n";
        std::cout << "    CoralEdgeTpu System Monitor\n";
        std::cout << "    Run ID: " << run_id_ << "\n";
        std::cout << "==========================================\n";
        std::cout << std::endl;
        
        // Calculate time since run start using monotonic clock
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - run_start_time_);
        std::cout << "Run Time: +" << elapsed.count() << " ms" << std::endl;
        
        // Also show wall clock time for reference
        auto wall_now = std::chrono::system_clock::now();
        auto wall_time_t = std::chrono::system_clock::to_time_t(wall_now);
        std::cout << "Wall Time: " << std::put_time(std::localtime(&wall_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
        std::cout << std::endl;
        
        // Get current values status
        size_t current_raw_image_queue_depth = app_.raw_image_for_processor_queue_->size_approx();
        size_t current_tpu_inference_queue_depth = app_.tpu_inference_queue_->size_approx();
        size_t current_detection_logic_queue_depth = app_.detection_results_for_logic_queue_->size_approx();
        size_t current_overlaid_video_queue_depth = app_.overlaid_video_queue_->size_approx();
        
        // Calculate queue in/out rates
        int raw_image_queue_in = 0;
        int raw_image_queue_out = 0;
        int tpu_inference_queue_in = 0;
        int tpu_inference_queue_out = 0;
        int detection_logic_queue_in = 0;
        int detection_logic_queue_out = 0;
        int overlaid_video_queue_in = 0;
        int overlaid_video_queue_out = 0;
        
        // ... (module rates calculation) ...
        // Calculate module rates based on the current FPS/IPS/CPS values
        int camera_fps = 0;
        int inference_ips = 0;
        int logic_cps = 0;
        
        if (app_.get_primary_camera() && app_.get_primary_camera()->is_running()) {
            camera_fps = app_.get_primary_camera()->frame_rate_.load();
        }
        if (app_.get_inference_engine() && app_.get_inference_engine()->is_running()) {
            inference_ips = app_.get_inference_engine()->get_current_inference_rate();
        }
        if (app_.get_logic_module() && app_.get_logic_module()->is_running()) {
            logic_cps = app_.get_logic_module()->logic_rate_.load();
        }
        
        // Calculate actual throughput based on queue depth changes
        // This is an approximation since we don't have direct counters for push/pop operations
        if (!first_update_) {
            // ... (throughput calculation) ...
            long long raw_image_change = static_cast<long long>(current_raw_image_queue_depth) - static_cast<long long>(prev_raw_image_queue_depth_);
            // Estimate: if queue depth increased, at least that many items went in beyond those that went out
            // if queue depth decreased, at least that many items went out beyond those that went in
            if (raw_image_change > 0) {
                // Queue increased: more items came in than went out
                // Approximate: in = camera_fps, out = in - change
                raw_image_queue_in = camera_fps; // Estimate based on camera rate
                raw_image_queue_out = std::max(0, raw_image_queue_in - static_cast<int>(raw_image_change));
            } else if (raw_image_change < 0) {
                // Queue decreased: more items went out than came in
                // Approximate: out = estimated from downstream, in = out + abs(change)
                raw_image_queue_out = camera_fps; // This is a better estimate when queue is decreasing
                raw_image_queue_in = std::max(0, raw_image_queue_out + static_cast<int>(-raw_image_change));
            } else {
                // Queue depth unchanged: in ~ out (steady state)
                raw_image_queue_in = camera_fps;
                raw_image_queue_out = camera_fps;
            }
            
            long long tpu_inference_change = static_cast<long long>(current_tpu_inference_queue_depth) - static_cast<long long>(prev_tpu_inference_queue_depth_);
            if (tpu_inference_change > 0) {
                tpu_inference_queue_in = inference_ips; // Estimate based on inference rate
                tpu_inference_queue_out = std::max(0, tpu_inference_queue_in - static_cast<int>(tpu_inference_change));
            } else if (tpu_inference_change < 0) {
                tpu_inference_queue_out = inference_ips; // Estimate when queue is decreasing
                tpu_inference_queue_in = std::max(0, tpu_inference_queue_out + static_cast<int>(-tpu_inference_change));
            } else {
                tpu_inference_queue_in = inference_ips;
                tpu_inference_queue_out = inference_ips;
            }
            
            long long detection_logic_change = static_cast<long long>(current_detection_logic_queue_depth) - static_cast<long long>(prev_detection_logic_queue_depth_);
            if (detection_logic_change > 0) {
                detection_logic_queue_in = inference_ips; // From inference output
                detection_logic_queue_out = std::max(0, detection_logic_queue_in - static_cast<int>(detection_logic_change));
            } else if (detection_logic_change < 0) {
                detection_logic_queue_out = inference_ips; // Estimate when queue is decreasing
                detection_logic_queue_in = std::max(0, detection_logic_queue_out + static_cast<int>(-detection_logic_change));
            } else {
                detection_logic_queue_in = inference_ips;
                detection_logic_queue_out = inference_ips;
            }
            
            long long overlaid_video_change = static_cast<long long>(current_overlaid_video_queue_depth) - static_cast<long long>(prev_overlaid_video_queue_depth_);
            if (overlaid_video_change > 0) {
                overlaid_video_queue_in = inference_ips; // From logic output
                overlaid_video_queue_out = std::max(0, overlaid_video_queue_in - static_cast<int>(overlaid_video_change));
            } else if (overlaid_video_change < 0) {
                overlaid_video_queue_out = inference_ips; // Estimate when queue is decreasing
                overlaid_video_queue_in = std::max(0, overlaid_video_queue_out + static_cast<int>(-overlaid_video_change));
            } else {
                overlaid_video_queue_in = inference_ips;
                overlaid_video_queue_out = inference_ips;
            }
        } else {
            // On first update, just store the values
            first_update_ = false;
        }
        
        // ... (module in/out calculation) ...
        // For modules, calculate proper in/out values based on actual module rates and queue dynamics
        // Camera Module: Input is frames captured, Output is frames pushed to raw image queue
        int camera_module_in = camera_fps;
        int camera_module_out = raw_image_queue_in; // What goes into the raw image queue comes from camera
        
        // Inference Module: Input is frames from raw image queue, Output is inference results to TPU queue
        int inference_module_in = raw_image_queue_out; // What inference pulls from raw image queue
        int inference_module_out = tpu_inference_queue_in; // What inference pushes to TPU queue
        
        // Logic Module: Input is detection results, Output is logic results
        int logic_module_in = inference_ips; // Pulls from inference output conceptually
        int logic_module_out = detection_logic_queue_in; // What logic pushes to next queue
        
        // Store current values for next iteration
        prev_raw_image_queue_depth_ = current_raw_image_queue_depth;
        prev_tpu_inference_queue_depth_ = current_tpu_inference_queue_depth;
        prev_detection_logic_queue_depth_ = current_detection_logic_queue_depth;
        prev_overlaid_video_queue_depth_ = current_overlaid_video_queue_depth;
        
        // ... (active determination) ...
        // Calculate actual module status based on processing activity
        bool camera_active = camera_module_in > 0 || camera_module_out > 0;
        bool inference_active = inference_module_in > 0 || inference_module_out > 0;
        bool logic_active = logic_module_in > 0 || logic_module_out > 0;

        // ... (cold-start detection) ...
        // Update persistent flags for cold-start detection
        if (camera_active) camera_seen_ = true;
        if (inference_active) inference_seen_ = true;
        if (logic_active) logic_seen_ = true;

        // ... (pipeline initialized check) ...
        // Track initialization phase
        if (!pipeline_initialized_) {
            if (camera_seen_ && inference_seen_ && logic_seen_) {
                pipeline_initialized_ = true;
            }
        }
        
        // ... (time calculation) ...
        // Calculate time since run start for relative timestamps
        auto current_monotonic_time = std::chrono::steady_clock::now();
        auto elapsed_since_run_start = std::chrono::duration_cast<std::chrono::milliseconds>(current_monotonic_time - run_start_time_);
        
        // Calculate a reference time point based on the current system time and elapsed run time
        // This allows us to convert absolute timestamps to relative times since run start
        auto now_sys = std::chrono::system_clock::now();
        auto now_sys_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
        auto reference_time_since_run_start = now_sys_ms - elapsed_since_run_start.count();
        
        // ... (Camera Module Status display) ...
        // Enhanced status determination with starvation/blocked detection
        // Camera Module Status
        std::cout << "[Camera Module]" << std::endl;
        if (app_.get_primary_camera() && app_.get_primary_camera()->is_running()) {
            if (camera_active) {
                std::cout << "  Status: RUNNING" << std::endl;
            } else if (!pipeline_initialized_) {
                std::cout << "  Status: INITIALIZING" << std::endl;  // Show initializing during startup
            } else if (camera_fps == 0 && app_.get_primary_camera()->frame_rate_.load() == 0) {
                // No frames being captured - could be blocked or starved
                std::cout << "  Status: STARVED" << std::endl;
            } else {
                std::cout << "  Status: IDLE" << std::endl;
            }
            std::cout << "  Frame Rate: " << camera_fps << " FPS" << std::endl;
            // Calculate relative time for last frame timestamp
            long long last_frame_absolute = app_.get_primary_camera()->last_frame_timestamp_;
            long long relative_frame_time = last_frame_absolute - reference_time_since_run_start;
            std::cout << "  Last Frame: +" << relative_frame_time << " ms (relative to run start)" << std::endl;
            std::cout << "  Throughput: In: " << camera_module_in << " | Out: " << camera_module_out << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // ... (Inference Module Status display) ...
        // Inference Module Status
        std::cout << "[Inference Module]" << std::endl;
        if (app_.get_inference_engine() && app_.get_inference_engine()->is_running()) {
            if (inference_active) {
                std::cout << "  Status: RUNNING" << std::endl;
            } else if (!pipeline_initialized_) {
                std::cout << "  Status: INITIALIZING" << std::endl;  // Show initializing during startup
            } else if (inference_ips == 0 && app_.get_inference_engine()->get_current_inference_rate() == 0) {
                // No inferences being processed - check if input queue is empty (starved) or full (blocked)
                size_t input_queue_depth = app_.raw_image_for_processor_queue_->size_approx();
                if (input_queue_depth == 0) {
                    std::cout << "  Status: STARVED" << std::endl;
                } else {
                    std::cout << "  Status: BLOCKED" << std::endl;
                }
            } else {
                std::cout << "  Status: IDLE" << std::endl;
            }
            std::cout << "  Inference Rate: " << inference_ips << " IPS" << std::endl;
            // Show actual inference execution time (not delta between inferences)
            long long inference_time_us = app_.get_inference_engine()->get_inference_timing_us();
            std::cout << "  Inference Time: " << inference_time_us << " μs (execution time)" << std::endl;
            // Calculate frame delta (time between consecutive inference completions)
            uint64_t last_inference_ns = app_.get_inference_engine()->get_last_inference_timestamp_ns();
            uint64_t prev_inference_ns = prev_last_inference_timestamp_ns_;
            uint64_t frame_delta_us = (last_inference_ns > prev_inference_ns)
                ? (last_inference_ns - prev_inference_ns) / 1000
                : 0;
            std::cout << "  Frame Delta: " << frame_delta_us << " μs (between completions)" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // ... (Logic Module Status display) ...
        // Logic Module Status
        std::cout << "[Logic Module]" << std::endl;
        if (app_.get_logic_module() && app_.get_logic_module()->is_running()) {
            if (logic_active) {
                std::cout << "  Status: RUNNING" << std::endl;
            } else if (!pipeline_initialized_) {
                std::cout << "  Status: INITIALIZING" << std::endl;  // Show initializing during startup
            } else if (logic_cps == 0 && app_.get_logic_module()->logic_rate_.load() == 0) {
                // No logic being processed - check if input queue is empty (starved) or full (blocked)
                size_t input_queue_depth = app_.detection_results_for_logic_queue_->size_approx();
                if (input_queue_depth == 0) {
                    std::cout << "  Status: STARVED" << std::endl;
                } else {
                    std::cout << "  Status: BLOCKED" << std::endl;
                }
            } else {
                std::cout << "  Status: IDLE" << std::endl;
            }
            std::cout << "  Logic Rate: " << logic_cps << " CPS" << std::endl;
            // Calculate relative time for last logic timestamp
            long long last_logic_absolute = app_.get_logic_module()->last_logic_timestamp_ns_.load() / 1000000;
            long long relative_logic_time = last_logic_absolute - reference_time_since_run_start;
            std::cout << "  Last Logic: +" << relative_logic_time << " ms (relative to run start)" << std::endl;
            std::cout << "  Throughput: In: " << logic_module_in << " | Out: " << logic_module_out << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // ... (Queue Throughput Information display) ...
        // Queue Throughput Information
        std::cout << "[Queue Throughput]" << std::endl;
        std::cout << "  Raw Image Queue: In: " << raw_image_queue_in << " | Out: " << raw_image_queue_out << std::endl;
        std::cout << "  TPU Inference Queue: In: " << tpu_inference_queue_in << " | Out: " << tpu_inference_queue_out << std::endl;
        std::cout << "  Detection Logic Queue: In: " << detection_logic_queue_in << " | Out: " << detection_logic_queue_out << std::endl;
        std::cout << "  Overlaid Video Queue: In: " << overlaid_video_queue_in << " | Out: " << overlaid_video_queue_out << std::endl;
        // H264 Output Queue disabled for TPU-only Mk VI build
        std::cout << std::endl;
        
        // ... (Queue Drop Counters display) ...
        // Queue Drop Counters for proper accounting
        std::cout << "[Queue Drop Counters]" << std::endl;
        if (app_.get_primary_camera()) {
            std::cout << "  Main Stream Drops: " << app_.get_primary_camera()->get_main_stream_drop_count() << std::endl;
            std::cout << "  TPU Stream Drops: " << app_.get_primary_camera()->get_tpu_stream_drop_count() << std::endl;
        } else {
            std::cout << "  Main/TPU Stream Drops: N/A" << std::endl;
        }
        if (app_.get_inference_engine()) {
            std::cout << "  Logic Queue Drops: " << app_.get_inf_to_logic_dropped() << std::endl;
        } else {
            std::cout << "  Logic Queue Drops: N/A" << std::endl;
        }
        // Display Viz->Fbdev drops
        std::cout << "  Viz->Fbdev Drops: " << app_.get_viz_to_fbdev_dropped() << std::endl;
        std::cout << std::endl;
        
        // ... (Camera Invariant Check display) ...
        // Queue Accounting Invariant Check: produced == consumed + dropped
        std::cout << "[Queue Accounting Invariants]" << std::endl;
        
        // Declare pass variables outside if blocks for anomaly detection
        bool v_pass = false, t_pass = false, p_pass = false, logic_pass = false, overlay_pass = false;
        
        if (app_.get_primary_camera()) {
            // STAGE 1: Camera -> Processors
            int64_t v_p = app_.get_cam_to_viz_produced();
            int64_t v_c = app_.get_cam_to_viz_consumed();
            int64_t v_d = app_.get_cam_to_viz_dropped();
            int64_t v_q = app_.main_video_queue_->size_approx();
            // int64_t v_f = (app_.get_visualization_processor() && app_.get_visualization_processor()->is_running()) ? 1 : 0;  // Unused but kept for future reference
            v_pass = (v_p == (v_c + v_d + v_q)); // Simplified for clarity, tolerance handled mentally

            int64_t t_p = app_.get_cam_to_tpu_proc_produced();
            int64_t t_c = app_.get_cam_to_tpu_proc_consumed();
            int64_t t_d = app_.get_cam_to_tpu_proc_dropped();
            int64_t t_q = app_.raw_image_for_processor_queue_->size_approx();
            // int64_t t_f = (app_.get_image_processor() && app_.get_image_processor()->is_running()) ? 1 : 0;  // Unused but kept for future reference
            t_pass = (t_p == (t_c + t_d + t_q));

            std::cout << "  S1: Cam->Viz | P:" << v_p << " C:" << v_c << " D:" << v_d << " Q:" << v_q 
                      << " | " << (v_pass ? "PASS" : "WAIT") << std::endl;
            std::cout << "  S1: Cam->TPU | P:" << t_p << " C:" << t_c << " D:" << t_d << " Q:" << t_q 
                      << " | " << (t_pass ? "PASS" : "WAIT") << std::endl;

            // STAGE 2: TPU Processor -> Inference Engine
            int64_t p_p = app_.get_proc_to_inf_produced();
            int64_t p_c = app_.get_proc_to_inf_consumed();
            int64_t p_d = app_.get_proc_to_inf_dropped();
            int64_t p_q = app_.tpu_inference_queue_->size_approx();
            // int64_t p_f = (app_.get_inference_engine() && app_.get_inference_engine()->is_running()) ? 1 : 0;  // Unused but kept for future reference
            p_pass = (p_p == (p_c + p_d + p_q));

            std::cout << "  S2: Proc->Inf| P:" << p_p << " C:" << p_c << " D:" << p_d << " Q:" << p_q 
                      << " | " << (p_pass ? "PASS" : "WAIT") << std::endl;
        } else {
            std::cout << "  Camera Streams - N/A" << std::endl;
        }
        
        // STAGE 3: Inference Engine -> Logic/Overlay
        if (app_.get_inference_engine()) {
            int64_t inference_produced = app_.get_inf_to_logic_produced();
            int64_t logic_consumed = app_.get_inf_to_logic_consumed();
            int64_t overlay_consumed = app_.get_inf_to_overlay_consumed();
            
            // Stage 3 Drop
            int64_t logic_dropped = app_.get_inf_to_logic_dropped(); 
            int64_t overlay_dropped = app_.get_inference_engine()->get_overlay_queue_drop_count();
            
            int64_t logic_queue_depth = app_.detection_results_for_logic_queue_->size_approx();
            bool overlay_pending = app_.get_inference_engine()->has_overlay_pending();
            
            logic_pass = (inference_produced == (logic_consumed + logic_dropped + logic_queue_depth));
            overlay_pass = (inference_produced == (overlay_consumed + overlay_dropped + (overlay_pending ? 1 : 0)));
            
            std::cout << "  S3: Inf->Logic| P:" << inference_produced << " C:" << logic_consumed << " D:" << logic_dropped << " Q:" << logic_queue_depth
                      << " | " << (logic_pass ? "PASS" : "WAIT") << std::endl;
            
            // New invariant for Visualization Processor -> Fbdev Display
            int64_t viz_p = app_.get_viz_to_fbdev_produced();
            int64_t viz_c = app_.get_viz_to_fbdev_consumed();
            int64_t viz_d = app_.get_viz_to_fbdev_dropped();
            int64_t viz_q = app_.overlaid_video_queue_->size_approx();
            bool viz_fbdev_pass = (viz_p == (viz_c + viz_d + viz_q));

            std::cout << "  S4: Viz->Fbdev | P:" << viz_p << " C:" << viz_c << " D:" << viz_d << " Q:" << viz_q
                      << " | " << (viz_fbdev_pass ? "PASS" : "WAIT") << std::endl;

        }
        std::cout << std::endl;
        
        // Orientation Sensor Status
        std::cout << "[Orientation Sensor]" << std::endl;
        if (app_.get_orientation_sensor() && app_.get_orientation_sensor()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
            // Get latest orientation data
            OrientationData orientation_data = app_.get_orientation_sensor()->get_latest_orientation_data();
            std::cout << "  Yaw: " << std::fixed << std::setprecision(2) << orientation_data.yaw << "°" << std::endl;
            std::cout << "  Pitch: " << std::fixed << std::setprecision(2) << orientation_data.pitch << "°" << std::endl;
            std::cout << "  Roll: " << std::fixed << std::setprecision(2) << orientation_data.roll << "°" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // System Monitor Status
        std::cout << "[System Monitor]" << std::endl;
        if (app_.get_system_monitor() && app_.get_system_monitor()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // H264 Encoder Status (Removed for Mk VI build)
        /*
        std::cout << "[H264 Encoder]" << std::endl;
        if (app_.get_h264_encoder() && app_.get_h264_encoder()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        */
        
        // Image Processor Status
        std::cout << "[Image Processor]" << std::endl;
        if (app_.get_image_processor() && app_.get_image_processor()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // Visualization Processor Status
        std::cout << "[Visualization Processor]" << std::endl;
        if (app_.get_visualization_processor() && app_.get_visualization_processor()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // Keyboard Monitor Status
        std::cout << "[Keyboard Monitor]" << std::endl;
        if (app_.get_keyboard_monitor() && app_.get_keyboard_monitor()->is_running()) {
            std::cout << "  Status: RUNNING" << std::endl;
        } else {
            std::cout << "  Status: STOPPED" << std::endl;
        }
        std::cout << std::endl;
        
        // ========================================
        // INFERENCE MONITOR SECTION
        // ========================================
        bool tpu_enabled = app_.get_config_loader().get_enable_tpu_inference();
        if (tpu_enabled) {
            std::cout << "[TPU CoralEdgeTpu Monitor]" << std::endl;
        } else {
            std::cout << "[OPENCV-BYPASS] OpenCV Classical Vision Inference" << std::endl;
        }
        
        bool anomaly_detected = false;
        std::stringstream anomaly_report;
        
        if (tpu_enabled) {
            // TPU-specific anomaly checks
            if (current_tpu_inference_queue_depth > 100) {
                anomaly_detected = true;
                anomaly_report << "  ANOMALY: TPU Inference Queue STALLED (depth=" << current_tpu_inference_queue_depth << ")\n";
            }
            
            if (app_.get_inference_engine() && app_.get_inference_engine()->is_running()) {
                int tpu_rate = app_.get_inference_engine()->get_current_inference_rate();
                if (tpu_rate == 0 && current_tpu_inference_queue_depth > 0) {
                    anomaly_detected = true;
                    anomaly_report << "  ANOMALY: TPU Throughput STALLED (0 IPS with queue depth=" << current_tpu_inference_queue_depth << ")\n";
                } else if (tpu_rate < 10 && current_tpu_inference_queue_depth > 50) {
                    anomaly_detected = true;
                    anomaly_report << "  ANOMALY: TPU Throughput DEGRADED (" << tpu_rate << " IPS, queue=" << current_tpu_inference_queue_depth << ")\n";
                }
            }
            
            // TPU temperature check
            if (app_.get_system_monitor()) {
                float tpu_temp = app_.get_system_monitor()->get_latest_tpu_temp();
                if (tpu_temp > 80.0f) {
                    anomaly_detected = true;
                    anomaly_report << "  ANOMALY: TPU OVERHEATING (" << std::fixed << std::setprecision(1) << tpu_temp << "C)\n";
                }
            }
        } else {
            // OpenCV-specific checks
            std::cout << "  Inference Backend: OpenCV (adaptive threshold + contours)" << std::endl;
            std::cout << "  Resolution: 1280x720" << std::endl;
            std::cout << "  Algorithm: Circularity-based target detection" << std::endl;
            
            // Check for inference stalls (any inference queue buildup)
            if (current_tpu_inference_queue_depth > 50) {
                anomaly_detected = true;
                anomaly_report << "  ANOMALY: Inference Queue BACKED UP (depth=" << current_tpu_inference_queue_depth << ")\n";
            }
        }
        
        if (anomaly_detected) {
            std::cout << "  *** ANOMALIES DETECTED ***\n";
            std::cout << anomaly_report.str();
            std::cout << "  [Auto-recovery may be triggered]\n";
        } else {
            std::cout << "  Status: ALL SYSTEMS NORMAL\n";
            std::cout << "  Inference Queue Depth: " << current_tpu_inference_queue_depth << "\n";
            if (app_.get_inference_engine()) {
                int rate = app_.get_inference_engine()->get_current_inference_rate();
                std::cout << "  Throughput: " << rate << " IPS\n";
            }
        }
        std::cout << std::endl;
        
        // Pipeline Timing Information
        std::cout << "[Pipeline Timing]" << std::endl;
        if (app_.get_primary_camera()) {
            std::cout << "  Capture: " << std::to_string(app_.get_primary_camera()->get_capture_timing_us()) << " μs" << std::endl;
        } else {
            std::cout << "  Capture: N/A" << std::endl;
        }
        if (app_.get_image_processor()) {
            std::cout << "  Queue pop: " << std::to_string(app_.get_image_processor()->get_queue_pop_timing_us()) << " μs" << std::endl;
            std::cout << "  Pre-processing: " << std::to_string(app_.get_image_processor()->get_preprocess_timing_us()) << " μs" << std::endl;
        } else {
            std::cout << "  Queue pop: N/A" << std::endl;
            std::cout << "  Pre-processing: N/A" << std::endl;
        }
        if (app_.get_inference_engine()) {
            std::cout << "  Inference: " << std::to_string(app_.get_inference_engine()->get_inference_timing_us()) << " μs" << std::endl;
        } else {
            std::cout << "  Inference: N/A" << std::endl;
        }
        if (app_.get_primary_camera()) {
            std::cout << "  Total loop: " << std::to_string(app_.get_primary_camera()->get_total_loop_timing_us()) << " μs" << std::endl;
        } else {
            std::cout << "  Total loop: N/A" << std::endl;
        }
        std::cout << std::endl;
        
        // Update prev values for next iteration
        prev_last_inference_timestamp_ns_ = app_.get_inference_engine()->get_last_inference_timestamp_ns();
        
        // Wait before next update - break this sleep into smaller chunks to be more responsive to shutdown
        for (int i = 0; i < 20 && running_; ++i) {  // 20 iterations of 50ms = 1000ms total
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}