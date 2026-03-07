// Verified headers: [termios.h, application_supervisor.h, camera_capture.h, config_loader.h...]
// Verification timestamp: 2026-01-19
#ifndef APPLICATION_H
#define APPLICATION_H

#include "config_loader.h"

#include <atomic>
#include <mutex>
#include <map>
#include <future>
#include <list>         // For std::list
#include <functional>   // For std::reference_wrapper

// Include signal handling headers
#include <signal.h>
#include <pthread.h>

// Forward declarations for modules to avoid circular dependencies and transitive includes
class InferenceEngine;
class CameraCapture;

// Module headers
#include "image_processor.h"
#include "fbdev_display.h" // Use fbdev for display on Pi 5
#include "orientation_sensor.h"
#include "logic.h"
#include "system_monitor.h"
#include "keyboard_monitor.h"
#include "monitor/monitor.h" // Assuming Monitor is in src/monitor/monitor.h
#include "discovery_module.h"
#include "application_supervisor.h"
#include "config_loader.h"
#include "lockfree_queue.h" // For ImageQueue
#include "memory_pool_limits.h"

extern std::atomic<bool> g_running;

/**
 * @brief Een applicatieklasse die de volledige CoralEdgeTpu-pijplijn beheert.
 * Deze klasse is verantwoordelijk voor het initialiseren, configureren, starten
 * en stoppen van alle modules in de beeldverwerkings- en inference-pijplijn.
 * Het centraliseert de setup-logica om de `main`-functie schoon te houden.
 */
class Application {
public:
    Application(int argc, char** argv);
    ~Application();

    /**
     * @brief Start de volledige applicatiepijplijn.
     *
     * Initialiseert en start alle modules in de juiste volgorde. Registreert
     * de modules bij de supervisor voor een graceful shutdown.
     *
     * @return 0 bij succes, 1 bij een fout.
     */
    int run();
    void stop();

public:
    // Expose member variables for Monitor class
    std::unique_ptr<ImageProcessor> image_processor_; // New module
    std::unique_ptr<InferenceEngine> inference_engine_;
    std::unique_ptr<CameraCapture> primary_camera_;
    std::unique_ptr<FbdevDisplay> fbdev_display_; // fbdev display for proper HDMI/display control
    // std::unique_ptr<VideoOverlayProcessor> overlay_processor_;
    // std::unique_ptr<H264Encoder> h264_encoder_;  // Removed for Mk VI build

    // std::unique_ptr<MpegTsServer> mpegts_server_;  // Removed for Mk VI build
    std::shared_ptr<OrientationSensor> orientation_sensor_;
    std::unique_ptr<LogicModule> logic_module_;
    std::unique_ptr<SystemMonitor> system_monitor_;
    std::unique_ptr<KeyboardMonitor> keyboard_monitor_;
    std::unique_ptr<Monitor> monitor_;
    std::unique_ptr<DiscoveryModule> discovery_module_; // Added for auto-discovery
    std::unique_ptr<ImageProcessor> visualization_processor_; // Added for visualization
    std::shared_ptr<ImageQueue> main_video_queue_; // Added queue
    // Queues (moved to public for monitor access)
    std::shared_ptr<ImageQueue> raw_image_for_processor_queue_; // New queue
    std::shared_ptr<ImageQueue> tpu_inference_queue_;

    TripleBuffer<DetectionResults> detection_results_for_overlay_buffer_;
    TripleBuffer<OverlayBallisticPoint> ballistic_points_for_overlay_buffer_;
    LatestImageBuffer latest_image_buffer_; // Latest-wins image pipeline (drops old frames)
    std::shared_ptr<DetectionResultsQueue> detection_results_for_logic_queue_;
    std::unique_ptr<DetectionOverlayQueue> overlay_queue_;
    std::shared_ptr<ImageQueue> overlaid_video_queue_;
    std::string dynamic_phone_ip_; // Added to store phone IP from command line

private:
    void release_camera_resources();
    void clear_telemetry_sockets();
    
    // Enhanced cleanup functions
    void pre_launch_cleanup();
    void post_shutdown_cleanup();
    void aggressive_resource_cleanup();
    void memory_leak_detection();
    void temporary_file_cleanup();
    void cleanup_ipc_resources();
    void cleanup_shared_memory();
    void cleanup_zombie_processes();
    void generate_cleanup_report();
    
    bool terminate_existing_instances();
    
    void setup_pools_and_queues();
    bool initialize_modules(const std::string& model_path, const std::string& labels_path);
    bool start_modules();
    void register_shutdown_handlers();
    void main_loop();
    
    // Additional monitoring functions
    void check_display_starvation();
    void monitor_queue_depths();
    void enforce_max_latency();
    void check_thread_stalls();
    void drain_queues();
    void debug_queue_monitoring();
    void debug_buffer_pool_monitoring();
    void run_debugging_pipeline();
    
    // Recovery mechanisms
    void recovery_thread_func();
    bool restart_camera_subsystem();
    bool restart_inference_subsystem();
    bool restart_logic_subsystem();
    bool restart_image_processor_subsystem();
    bool restart_orientation_subsystem();
    bool restart_visualization_subsystem(); // Added method
    
    int argc_;
    char** argv_;

    ConfigLoader config_loader_;
    ApplicationSupervisor supervisor_;
    std::unique_ptr<aurore::memory::MemoryPoolLimits> memory_limits_;
    
    // Buffer Pools
    std::shared_ptr<BufferPool<uint8_t>> image_pool_;
    std::shared_ptr<BufferPool<DetectionResult>> detection_pool_;
    
    // Object Pools for lock-free queue elements
    std::shared_ptr<ObjectPool<ImageData>> image_data_pool_;
    std::shared_ptr<ObjectPool<ResultToken>> result_token_pool_;

    // Thread for consuming overlay detection results
    std::thread overlay_consumer_thread_;
    std::atomic<bool> overlay_consumer_running_{false};
    void overlay_queue_consumer_thread_func();
    std::list<std::reference_wrapper<ImageQueue>> main_image_output_queues_;
    
    // Thread for consuming frames for Fbdev display (replaces DRM consumer)
    std::thread drm_consumer_thread_; // Renamed to fbdev_consumer_thread_ for clarity
    std::atomic<bool> drm_consumer_running_{false}; // Renamed to fbdev_consumer_running_
    void fbdev_queue_consumer_thread_func(); // Renamed from drm_queue_consumer_thread_func

    // Recovery mechanisms
    std::atomic<bool> recovery_running_{false};
    std::atomic<bool> recovery_enabled_{false}; // New flag to control when recovery is active
    std::thread recovery_thread_;
    std::mutex recovery_mutex_;
    std::condition_variable recovery_cv_;
    
    // Detector supervision
    std::thread detector_supervisor_thread_;
    std::atomic<bool> detector_supervisor_running_{false};
    std::atomic<pid_t> detector_pid_{-1};
    void detector_supervisor_thread_func();
    bool start_detector_process();
    void stop_detector_process();
    bool is_detector_running();
    
    // Recovery counters for each subsystem
    std::map<std::string, int> recovery_attempts_;
    const int max_recovery_attempts_ = 5; // Maximum attempts per second

    std::vector<std::string> labels_;
    
public:
    // --- STAGE 1: Camera -> Processors ---
    // (One frame from camera produces ONE frame for EACH registered consumer queue)
    std::atomic<int64_t> cam_to_viz_produced_count_{0LL};
    std::atomic<int64_t> cam_to_viz_dropped_count_{0LL};
    std::atomic<int64_t> cam_to_viz_consumed_count_{0LL}; // by Viz Processor

    std::atomic<int64_t> cam_to_tpu_proc_produced_count_{0LL};
    std::atomic<int64_t> cam_to_tpu_proc_dropped_count_{0LL};
    std::atomic<int64_t> cam_to_tpu_proc_consumed_count_{0LL}; // by TPU Processor (ImageProcessor) or InferenceEngine (OpenCV)

    // --- STAGE 2: TPU Processor -> Inference Engine ---
    // (Each frame processed by TPU Processor produces ONE frame for Inference Engine)
    std::atomic<int64_t> proc_to_inf_produced_count_{0LL};
    std::atomic<int64_t> proc_to_inf_dropped_count_{0LL};
    std::atomic<int64_t> proc_to_inf_consumed_count_{0LL}; // by Inference Engine

    // --- STAGE 3: Inference Engine -> Logic/Overlay ---
    std::atomic<int64_t> inf_to_logic_produced_count_{0LL};
    std::atomic<int64_t> inf_to_logic_dropped_count_{0LL};
    std::atomic<int64_t> inf_to_logic_consumed_count_{0LL}; // by Logic Module

    std::atomic<int64_t> inf_to_overlay_produced_count_{0LL};
    std::atomic<int64_t> inf_to_overlay_dropped_count_{0LL};
    std::atomic<int64_t> inf_to_overlay_consumed_count_{0LL}; // by Visualization Processor (reads from triple buffer)
    
    std::atomic<int64_t> viz_to_fbdev_produced_count_{0LL};
    std::atomic<int64_t> viz_to_fbdev_dropped_count_{0LL};
    std::atomic<int64_t> viz_to_fbdev_consumed_count_{0LL};

    // Getter methods for Monitor class
    const std::unique_ptr<ImageProcessor>& get_image_processor() const { return image_processor_; }
    const std::unique_ptr<ImageProcessor>& get_visualization_processor() const { return visualization_processor_; }
    const std::unique_ptr<InferenceEngine>& get_inference_engine() const { return inference_engine_; }
    const std::unique_ptr<CameraCapture>& get_primary_camera() const { return primary_camera_; }
    // const std::unique_ptr<H264Encoder>& get_h264_encoder() const { return h264_encoder_; }  // Removed for Mk VI build
    const std::shared_ptr<OrientationSensor>& get_orientation_sensor() const { return orientation_sensor_; }
    const std::unique_ptr<LogicModule>& get_logic_module() const { return logic_module_; }
    const std::unique_ptr<SystemMonitor>& get_system_monitor() const { return system_monitor_; }
    const ConfigLoader& get_config_loader() const { return config_loader_; }
    LatestImageBuffer* get_latest_image_buffer() { return &latest_image_buffer_; }
    aurore::memory::MemoryPoolLimits* get_memory_limits() const { return memory_limits_.get(); }
    
    // Memory tracking methods for pipeline integration
    bool track_allocation(aurore::memory::MemoryPool pool, size_t size) {
        return memory_limits_ ? memory_limits_->allocate(pool, size) : false;
    }
    void track_deallocation(aurore::memory::MemoryPool pool, size_t size) {
        if (memory_limits_) memory_limits_->deallocate(pool, size);
    }
    
    const std::unique_ptr<KeyboardMonitor>& get_keyboard_monitor() const { return keyboard_monitor_; }

    // Getter methods for accessing detection and ballistic buffers
    TripleBuffer<DetectionResults>& get_detection_results_for_overlay_buffer() { return detection_results_for_overlay_buffer_; }
    TripleBuffer<OverlayBallisticPoint>& get_ballistic_points_for_overlay_buffer() { return ballistic_points_for_overlay_buffer_; }

    // Stage 1 Getters
    int64_t get_cam_to_viz_produced() const { return cam_to_viz_produced_count_.load(); }
    int64_t get_cam_to_viz_dropped() const { return cam_to_viz_dropped_count_.load(); }
    int64_t get_cam_to_viz_consumed() const { return cam_to_viz_consumed_count_.load(); }
    int64_t get_cam_to_tpu_proc_produced() const { return cam_to_tpu_proc_produced_count_.load(); }
    int64_t get_cam_to_tpu_proc_dropped() const { return cam_to_tpu_proc_dropped_count_.load(); }
    int64_t get_cam_to_tpu_proc_consumed() const { return cam_to_tpu_proc_consumed_count_.load(); }
    
    // Stage 2 Getters
    int64_t get_proc_to_inf_produced() const { return proc_to_inf_produced_count_.load(); }
    int64_t get_proc_to_inf_dropped() const { return proc_to_inf_dropped_count_.load(); }
    int64_t get_proc_to_inf_consumed() const { return proc_to_inf_consumed_count_.load(); }

    int64_t get_inf_to_logic_produced() const { return inf_to_logic_produced_count_.load(); }
    int64_t get_inf_to_logic_dropped() const { return inf_to_logic_dropped_count_.load(); }
    int64_t get_inf_to_logic_consumed() const { return inf_to_logic_consumed_count_.load(); }

    int64_t get_inf_to_overlay_produced() const { return inf_to_overlay_produced_count_.load(); }
    int64_t get_inf_to_overlay_dropped() const { return inf_to_overlay_dropped_count_.load(); }
    int64_t get_inf_to_overlay_consumed() const { return inf_to_overlay_consumed_count_.load(); }
    
    int64_t get_viz_to_fbdev_produced() const { return viz_to_fbdev_produced_count_.load(); }
    int64_t get_viz_to_fbdev_dropped() const { return viz_to_fbdev_dropped_count_.load(); }
    int64_t get_viz_to_fbdev_consumed() const { return viz_to_fbdev_consumed_count_.load(); }

    // Methods to update counters from modules
    void inc_cam_to_viz_produced() { cam_to_viz_produced_count_.fetch_add(1); }
    void inc_cam_to_viz_dropped() { cam_to_viz_dropped_count_.fetch_add(1); }
    void inc_cam_to_viz_consumed() { cam_to_viz_consumed_count_.fetch_add(1); }

    void inc_cam_to_tpu_proc_produced() { cam_to_tpu_proc_produced_count_.fetch_add(1); }
    void inc_cam_to_tpu_proc_dropped() { cam_to_tpu_proc_dropped_count_.fetch_add(1); }
    void inc_cam_to_tpu_proc_consumed() { cam_to_tpu_proc_consumed_count_.fetch_add(1); }

    void inc_proc_to_inf_produced() { proc_to_inf_produced_count_.fetch_add(1); }
    void inc_proc_to_inf_dropped() { proc_to_inf_dropped_count_.fetch_add(1); }
    void inc_proc_to_inf_consumed() { proc_to_inf_consumed_count_.fetch_add(1); }

    void inc_inf_to_logic_produced() { inf_to_logic_produced_count_.fetch_add(1); }
    void inc_inf_to_logic_dropped() { inf_to_logic_dropped_count_.fetch_add(1); }
    void inc_inf_to_logic_consumed() { inf_to_logic_consumed_count_.fetch_add(1); }

    void inc_inf_to_overlay_produced() { inf_to_overlay_produced_count_.fetch_add(1); }
    void inc_inf_to_overlay_dropped() { inf_to_overlay_dropped_count_.fetch_add(1); }
    void inc_inf_to_overlay_consumed() { inf_to_overlay_consumed_count_.fetch_add(1); }

    void inc_viz_to_fbdev_produced() { viz_to_fbdev_produced_count_.fetch_add(1); }
    void inc_viz_to_fbdev_dropped() { viz_to_fbdev_dropped_count_.fetch_add(1); }
    void inc_viz_to_fbdev_consumed() { viz_to_fbdev_consumed_count_.fetch_add(1); }

private:
};

#endif // APPLICATION_H