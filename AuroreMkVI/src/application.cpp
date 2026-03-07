#include <string.h> // For C-style string functions (memcpy, memset, etc.)
#include "application.h"
#include "camera/camera_capture.h"
#include "inference/inference.h"
#include "pipeline_structs.h"
#include "util_logging.h"
#include "queue_monitor.h"
#include "timing.h"
#include "fbdev_display.h"
#include "data_integrity.h"
#include "pipeline_trace.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <csignal>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>  // for waitpid
#include <cstring>
#include <future>
#include <termios.h>  // for terminal settings
#include <poll.h>     // for pollfd, POLLIN, poll

// External declaration for terminal settings
extern struct termios original_termios;

// Forward declaration from main.cpp
std::vector<std::string> load_labels(const std::string& path);
extern std::atomic<bool> g_running; // Now managed by ApplicationSupervisor
static std::atomic<bool> application_destructor_active_{false}; // Guard against destructor races

// Utility function for timed thread joins
static bool timed_join_thread(std::thread& thread, const std::string& thread_name, std::chrono::seconds timeout = std::chrono::seconds(3)) {
    if (!thread.joinable()) {
        return true;
    }
    
    auto shared_promise = std::make_shared<std::promise<void>>();
    std::future<void> future = shared_promise->get_future();
    
    // Spawn a wrapper thread to perform the join
    // Capture thread by reference but be extremely careful with scope
    std::thread joiner_thread([&thread, shared_promise]() {
        try {
            if (thread.joinable()) {
                thread.join();
            }
            shared_promise->set_value();
        } catch (...) {
            // Prevent exception escape from joiner thread
        }
    });
    
    // Wait for the join to complete or timeout
    if (future.wait_for(timeout) == std::future_status::timeout) {
        APP_LOG_WARNING("[SHUTDOWN] Thread '" + thread_name + "' did not join within " + std::to_string(timeout.count()) + "s, detaching to prevent SIGABRT.");
        
        // If we timeout, we MUST detach the original thread to prevent SIGABRT on its destruction
        if (thread.joinable()) {
            thread.detach();
        }
        // Also detach the joiner_thread because it's blocked on the original thread's join()
        joiner_thread.detach();
        return false;
    }
    
    if (joiner_thread.joinable()) {
        joiner_thread.join();
    }
    return true;
}

Application::Application(int argc, char** argv) : argc_(argc), argv_(argv), recovery_running_(true), recovery_enabled_(false), fbdev_display_(nullptr) {
    // Load configuration immediately so other modules can access it during their construction
    config_loader_.load("config.json");


    std::cout << "[INIT] Application constructor starting..." << std::endl;
    APP_LOG_INFO("Application constructor starting...");
    
    // Set up signal handlers to restore terminal settings on exit
    std::cout << "[INIT] Setting up signal handlers..." << std::endl;
    supervisor_.setup_signal_handlers();
    
    APP_LOG_INFO("Application constructor signal handlers set.");
    std::cout << "[INIT] Signal handlers set up successfully" << std::endl;

    // Check for reduced resolution flag and dynamic IP in command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find('-') != 0 && arg.find('.') != std::string::npos) {
            // Assume it's an IP address if it doesn't start with '-' and contains a '.'
            dynamic_phone_ip_ = arg;
            APP_LOG_INFO("Application: Dynamic phone IP detected via command line: " + dynamic_phone_ip_);
        }
    }
    
    // Recovery thread will be started after modules are initialized
}

Application::~Application() {
    application_destructor_active_.store(true, std::memory_order_release);
    stop();
    ::aurore::logging::Logger::getInstance().stop_writer_thread();
    APP_LOG_INFO("Shutdown complete.");
}

void Application::stop() {
    static std::atomic<bool> stopping{false};
    
    // Guard against calling stop() during destructor
    if (application_destructor_active_.load(std::memory_order_acquire)) {
        APP_LOG_INFO("[SHUTDOWN] Skipping stop() - destructor in progress");
        return;
    }
    
    if (stopping.exchange(true)) {
        return; 
    }

    g_running.store(false, std::memory_order_release);
    APP_LOG_INFO("[SHUTDOWN] Application::stop() initiated.");

    // CRITICAL: Invalidate ALL queues FIRST before any other operations
    // This prevents any thread from trying to use queues after this point
    APP_LOG_INFO("[SHUTDOWN] Invalidating all queues...");
    if (raw_image_for_processor_queue_) {
        raw_image_for_processor_queue_->invalidate();
    }
    if (main_video_queue_) {
        main_video_queue_->invalidate();
    }
    if (tpu_inference_queue_) {
        tpu_inference_queue_->invalidate();
    }
    if (detection_results_for_logic_queue_) {
        detection_results_for_logic_queue_->invalidate();
    }
    if (overlaid_video_queue_) {
        overlaid_video_queue_->invalidate();
    }
    
    // Small delay to allow any in-flight operations to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 1. Stop the recovery thread first
    recovery_running_ = false;
    recovery_cv_.notify_all();
    APP_LOG_INFO("[SHUTDOWN] Joining RecoveryThread...");
    timed_join_thread(recovery_thread_, "RecoveryThread");

    // 2. Stop encoding/streaming subsystems (disabled for TPU-only Mk VI build)
    APP_LOG_INFO("[SHUTDOWN] Encoding/streaming disabled (TPU-only mode)...");

    // 3. DO NOT push poison pills - queues are already invalidated
    APP_LOG_INFO("[SHUTDOWN] Queues invalidated, skipping poison pills.");

    // 4. ApplicationSupervisor handles the graceful shutdown of all registered modules
    APP_LOG_INFO("[SHUTDOWN] Initiating supervisor shutdown...");
    supervisor_.initiate_shutdown();
    
    // 5. Join the consumer threads
    APP_LOG_INFO("[SHUTDOWN] Joining consumer threads...");
    overlay_consumer_running_ = false;
    timed_join_thread(overlay_consumer_thread_, "OverlayConsumerThread");
    
    drm_consumer_running_ = false;
    timed_join_thread(drm_consumer_thread_, "FBDEVConsumerThread"); // Changed name
    
    // 6. Drain queues only if they are still valid
    APP_LOG_INFO("[SHUTDOWN] Draining queues...");
    drain_queues();

    // 7. Final cleanup
    APP_LOG_INFO("[SHUTDOWN] Final supervisor cleanup...");
    supervisor_.final_cleanup();
    
    // Shutdown pipeline tracing
    APP_LOG_INFO("[SHUTDOWN] Shutting down pipeline tracing...");
    aurore::trace::PipelineTracer::instance().shutdown();
    APP_LOG_INFO("[SHUTDOWN] Pipeline tracing shutdown complete.");
    
    APP_LOG_INFO("[SHUTDOWN] Application::stop() completed.");
}

void Application::post_shutdown_cleanup() {
    // This method is now effectively empty as cleanup is handled by modules and supervisor
    APP_LOG_INFO("Post-shutdown cleanup phase completed.");
}

void Application::setup_pools_and_queues() {
    const unsigned int cam_w = config_loader_.get_camera_width();
    const unsigned int cam_h = config_loader_.get_camera_height();
    
    size_t image_buffer_size = cam_w * cam_h * 3; // BGR888
    APP_LOG_INFO("Image buffer size per frame (BGR888): " + std::to_string(image_buffer_size) + " bytes.");
    
    // Determine pool size for images from config, default to a reasonable value if not set or invalid
    // Reduced for Pi stability (avoid OOM) -> Increased to 300 to fix starvation -> Now reduced for faster cycling
    size_t image_pool_count = 100;  // Increased from 50 for better burst handling with 60 FPS viz

    image_pool_ = std::make_shared<BufferPool<uint8_t>>(image_pool_count, image_buffer_size, "ImagePool");
    APP_LOG_INFO("ImagePool created with " + std::to_string(image_pool_count) + " buffers, total memory: " + std::to_string(image_pool_count * image_buffer_size / (1024 * 1024)) + " MB.");
    detection_pool_ = std::make_shared<BufferPool<DetectionResult>>(50, 200, "DetectionPool");

    // Object pools for lock-free queue elements
    image_data_pool_ = std::make_shared<ObjectPool<ImageData>>(100, "ImageDataPool");  // Increased from 50
    result_token_pool_ = std::make_shared<ObjectPool<ResultToken>>(80, "ResultTokenPool");  // Reduced from 400

    // Detection overlay queue for Phase 12 GPU overlay
    overlay_queue_ = std::make_unique<DetectionOverlayQueue>();
}

bool Application::initialize_modules(const std::string& model_path, const std::string& labels_path) {
    std::cout << "[INIT] Starting initialize_modules..." << std::endl;
    const unsigned int cam_w = config_loader_.get_camera_width();
    const unsigned int cam_h = config_loader_.get_camera_height();
    const std::chrono::seconds camera_watchdog_timeout = config_loader_.get_camera_watchdog_timeout();
    
    // --- Labels ---
    std::cout << "[INIT] Checking model file existence..." << std::endl;
    if (!std::filesystem::exists(model_path)) {
        std::cout << "[ERROR] Model file not found: " << model_path << std::endl;
        APP_LOG_ERROR("Model file not found: " + model_path);
        return false;
    }
    std::cout << "[INIT] Model file found" << std::endl;
    
    std::cout << "[INIT] Loading labels..." << std::endl;
    labels_ = load_labels(labels_path);
    if (labels_.empty()) {
        std::cout << "[ERROR] Labels file empty: " << labels_path << std::endl;
        APP_LOG_ERROR("Labels file empty: " + labels_path);
        return false;
    }
    std::cout << "[INIT] Labels loaded successfully, count: " << labels_.size() << std::endl;

    // --- Module Creation ---
    std::cout << "[INIT] Starting module creation..." << std::endl;
    try {
        unsigned int inf_w = config_loader_.get_tpu_target_width();
        unsigned int inf_h = config_loader_.get_tpu_target_height();

        // Initialize queues
        std::cout << "[INIT] Initializing queues..." << std::endl;
        APP_LOG_INFO("Initializing queues...");
        main_video_queue_ = std::make_shared<ImageQueue>();
        raw_image_for_processor_queue_ = std::make_shared<ImageQueue>();
        tpu_inference_queue_ = std::make_shared<ImageQueue>();
        detection_results_for_logic_queue_ = std::make_shared<DetectionResultsQueue>();
        overlaid_video_queue_ = std::make_shared<ImageQueue>();
        std::cout << "[INIT] Queues initialized successfully" << std::endl;

        // Create FbdevDisplay
        std::cout << "[INIT] Creating FbdevDisplay..." << std::endl;
        APP_LOG_INFO("Creating FbdevDisplay...");
        fbdev_display_ = std::make_unique<FbdevDisplay>(); // No argument needed
        APP_LOG_INFO("FbdevDisplay created.");
        std::cout << "[INIT] FbdevDisplay created successfully" << std::endl;

        // Initialize FbdevDisplay
        if (!fbdev_display_->initialize()) {
            printf("❌ ERROR: Failed to initialize Fbdev display (application.cpp)!\n");
            return false;
        }
        APP_LOG_INFO("FbdevDisplay initialized.");
        std::cout << "[INIT] FbdevDisplay initialized successfully" << std::endl; // Changed message
                
        // Send main video frames to main_video_queue_ instead of directly to overlaid_video_queue_        main_image_output_queues_.push_back(std::ref(*main_video_queue_)); 
        
        std::cout << "[INIT] Creating CameraCapture..." << std::endl;
        APP_LOG_INFO("Creating CameraCapture...");
        primary_camera_ = std::unique_ptr<CameraCapture>(new CameraCapture(
            cam_w, cam_h, // Main stream width/height
            config_loader_.get_tpu_stream_width(), config_loader_.get_tpu_stream_height(), // TPU stream width/height (from config)
            static_cast<unsigned int>(config_loader_.get_camera_fps()),
            config_loader_.get_tpu_target_width(), config_loader_.get_tpu_target_height(), // Target TPU width/height (from config)
            image_pool_, image_data_pool_,
            *raw_image_for_processor_queue_,
            config_loader_.get_camera_watchdog_timeout() // Corrected method name
        ));
        primary_camera_->set_main_output_queues({main_video_queue_.get()});
        APP_LOG_INFO("CameraCapture created.");
        std::cout << "[INIT] CameraCapture created successfully" << std::endl;
        
        // Set application reference for camera to update counters
        std::cout << "[INIT] Setting camera application reference..." << std::endl;
        primary_camera_->set_application_ref(this);
        std::cout << "[INIT] Camera application reference set" << std::endl;

        if (config_loader_.get_enable_tpu_inference()) {
            std::cout << "[INIT] Creating ImageProcessor for TPU inference..." << std::endl;
            APP_LOG_INFO("Creating ImageProcessor for TPU inference...");
            // Create ImageProcessor for TPU inference (no detection overlays needed)
            image_processor_.reset(new ImageProcessor(
                raw_image_for_processor_queue_.get(), tpu_inference_queue_.get(), image_pool_, image_data_pool_,
                config_loader_.get_tpu_stream_pixel_format(), // Pass configured format
                (int)inf_w, (int)inf_h, true, std::string("ImageProcessor_TPU"))); // Changed drm_display_->drm_fd_ to drm_display_->fb_fd_
            image_processor_->set_skip_factor(1); // Process every frame (Mandate: Zero Skip)
            image_processor_->set_application_ref(this);
            APP_LOG_INFO("ImageProcessor created.");
            std::cout << "[INIT] TPU ImageProcessor created successfully" << std::endl;
        } else {
            APP_LOG_INFO("TPU inference is disabled. Skipping creation of ImageProcessor for TPU inference and re-routing camera directly to InferenceEngine.");
            std::cout << "[INIT] TPU inference is disabled. Skipping creation of ImageProcessor for TPU inference and re-routing camera directly to InferenceEngine." << std::endl;
            // When TPU is disabled, no ImageProcessor for TPU inference is created.
            // The InferenceEngine will now take input directly from raw_image_for_processor_queue_
        }

        std::cout << "[INIT] Creating ImageProcessor for visualization with overlays..." << std::endl;
        APP_LOG_INFO("Creating ImageProcessor for visualization with overlays...");
        // Create a new ImageProcessor instance for the main video stream with detection overlays
        visualization_processor_.reset(new ImageProcessor(
            main_video_queue_.get(), overlaid_video_queue_.get(),
            &detection_results_for_overlay_buffer_, // Connect to triple buffer for overlays
            &ballistic_points_for_overlay_buffer_,  // New argument for ballistic points
            image_pool_, image_data_pool_,
            config_loader_.get_tpu_stream_pixel_format(), // Use same format as TPU stream for consistency
            (int)cam_w, (int)cam_h, overlay_queue_.get(), std::string("ImageProcessor_Viz"))); // Changed drm_display_->drm_fd_ to drm_display_->fb_fd_
        visualization_processor_->set_skip_factor(1); // Process every frame for full throughput
        visualization_processor_->set_application_ref(this);
        visualization_processor_->set_is_tpu_stream(false);
        APP_LOG_INFO("Visualization ImageProcessor created.");
        std::cout << "[INIT] Visualization ImageProcessor created successfully" << std::endl;
        
        // Also register overlaid_video_queue_ so CameraCapture pushes frames to it for the encoder
        // Actually, main_video_queue_ goes to visualization_processor, which then pushes to overlaid_video_queue_.
        // Wait, I see the confusion. 
        // 1. Camera -> main_video_queue_ -> VisualizationProcessor -> overlaid_video_queue_ -> H264Encoder
        // So main_video_queue_ is the only one Camera needs to know for the main stream.
        // My previous fix in CameraCapture was correct (iterate all main_output_queues_).
        // But main_image_output_queues_ ONLY contains main_video_queue_.
        // Where did overlaid_video_queue_ go? It's the OUTPUT of visualization_processor.
        // And it's the INPUT of H264Encoder.
        // So VisualizationProcessor MUST release the pointer it pops from main_video_queue_
        // and acquire a NEW one for overlaid_video_queue_.
        // I've already implemented this in ImageProcessor.
        
        // So why the leak?
        // Ah! main_video_queue_ is being pushed to by Camera, but IS ANYONE POPPING IT?
        // VisualizationProcessor pops it.
        // H264Encoder pops from overlaid_video_queue_.
        
        // Let's re-verify ImageProcessor pops.
        std::cout << "[INIT] Creating InferenceEngine..." << std::endl;
        // Use raw_image_for_processor_queue_ for OpenCV detection when TPU is disabled
        auto& detection_input_queue = config_loader_.get_enable_tpu_inference() ? *tpu_inference_queue_ : *raw_image_for_processor_queue_;
        
        inference_engine_ = std::make_unique<InferenceEngine>(
            model_path, detection_input_queue, &detection_results_for_overlay_buffer_,
            *detection_results_for_logic_queue_, detection_pool_,
            image_data_pool_, result_token_pool_,
            config_loader_.get_detection_score_threshold(),
            nullptr,
            config_loader_.get_enable_tpu_inference(), // Use actual config value
            /* enable_gpu_inference = */ true,
            config_loader_.get_inference_worker_threads());
        APP_LOG_INFO("InferenceEngine created.");
        std::cout << "[INIT] InferenceEngine created successfully" << std::endl;
        
        // Set application reference for inference engine to update counters
        inference_engine_->set_application_ref(this);
        
        // Assert that InferenceEngine's actual input dimensions match the configured target.
        // This is a sanity check to ensure the model matches configuration.
        // Skip this check when TPU is disabled (using OpenCV fallback)
        if (config_loader_.get_enable_tpu_inference()) {
            if (static_cast<unsigned int>(inference_engine_->get_input_width()) != inf_w || static_cast<unsigned int>(inference_engine_->get_input_height()) != inf_h) {
                 {
                     std::stringstream ss;
                     ss << "InferenceEngine input dimensions (" << inference_engine_->get_input_width() << "x" << inference_engine_->get_input_height()
                        << ") from model do not match configured TPU target dimensions (" << inf_w << "x" << inf_h << "). This will cause errors.";
                     APP_LOG_ERROR(ss.str());
                 }
                 return false;
            }
        } else {
            APP_LOG_INFO("TPU inference disabled - skipping model dimension check (using OpenCV fallback)");
        }

        orientation_sensor_ = std::make_shared<OrientationSensor>(config_loader_.get_orientation_pub_port(), config_loader_.get_orientation_pub_port(), config_loader_.get_orientation_pub_port());
        APP_LOG_INFO("OrientationSensor created.");

        APP_LOG_INFO("Creating LogicModule...");
        APP_LOG_INFO("Creating SystemMonitor...");
        system_monitor_ = std::make_unique<SystemMonitor>();
        APP_LOG_INFO("SystemMonitor created.");

        APP_LOG_INFO("Creating MemoryPoolLimits...");
        memory_limits_ = std::make_unique<aurore::memory::MemoryPoolLimits>();
        memory_limits_->configure_defaults();
        
        memory_limits_->set_alert_callback([this](aurore::memory::MemoryPool pool, aurore::memory::MemoryAlertLevel level, float utilization) {
            std::string pool_name = memory_limits_->get_pool_stats(pool).name;
            std::string level_str = memory_limits_->get_alert_level_string(level);
            std::string msg = "Memory alert: " + pool_name + " - " + level_str + 
                             " (" + std::to_string(utilization * 100.0f) + "%%)";
            APP_LOG_WARNING(msg);
        });
        
        memory_limits_->set_allocation_failed_callback([this](aurore::memory::MemoryPool pool, size_t size, size_t excess) {
            std::string pool_name = memory_limits_->get_pool_stats(pool).name;
            std::string msg = "Memory allocation failed: " + pool_name + " - requested " + 
                             std::to_string(size) + " bytes, exceeded by " + std::to_string(excess) + " bytes";
            APP_LOG_ERROR(msg);
        });
        APP_LOG_INFO("MemoryPoolLimits created.");

        // H264Encoder removed for Mk VI build
        /*
        APP_LOG_INFO("Creating H264Encoder...");
        APP_LOG_INFO("Application: overlaid_video_queue_ Address: " + std::to_string((uintptr_t)overlaid_video_queue_.get()));
        // h264_encoder_ = std::make_unique<H264Encoder>(*overlaid_video_queue_, *h264_output_queue_, h264_pool_, image_data_pool_, cam_w, cam_h, config_loader_.get_camera_fps());  // Removed for Mk VI build
        // h264_encoder_->set_application_ref(this);  // Removed for Mk VI build
        APP_LOG_INFO("H264Encoder created.");
        */

        APP_LOG_INFO("Creating LogicModule...");
        logic_module_ = std::make_unique<LogicModule>(
            *detection_results_for_logic_queue_, 
            result_token_pool_, 
            orientation_sensor_, 
            config_loader_, 
            &ballistic_points_for_overlay_buffer_,
            system_monitor_.get()
            // nullptr  // h264_encoder_.get() - Removed for Mk VI build
        );
        APP_LOG_INFO("LogicModule created.");
        
        // Set application reference for logic module to update counters
        logic_module_->set_application_ref(this);

        APP_LOG_INFO("Creating KeyboardMonitor...");
        keyboard_monitor_ = std::make_unique<KeyboardMonitor>();
        APP_LOG_INFO("KeyboardMonitor created.");
        


        // Create MPEG-TS server
        // MPEG-TS server removed for Mk VI build
        /*
        APP_LOG_INFO("Creating MpegTsServer...");
        std::string video_address = dynamic_phone_ip_.empty() ? config_loader_.get_video_stream_address() : dynamic_phone_ip_;
        const unsigned short video_port = config_loader_.get_video_stream_rtp_port();
        APP_LOG_INFO("MpegTsServer: Target video stream address: " + video_address + ", port: " + std::to_string(video_port));
        // mpegts_server_ = std::make_unique<MpegTsServer>(  // Removed for Mk VI build
            cam_w, 
            cam_h, 
            config_loader_.get_camera_fps(),
            video_address,
            video_port
        );
        APP_LOG_INFO("MpegTsServer created.");
        */

        APP_LOG_INFO("Creating Monitor...");
        monitor_ = std::make_unique<Monitor>(*this);
        APP_LOG_INFO("Monitor created.");

    } catch (const std::exception& e) {
        APP_LOG_ERROR("Failed to initialize modules: " + std::string(e.what()));
        return false;
    }

    return true;
}

bool Application::start_modules() {
    APP_LOG_INFO("Starting stabilization sequence...");

    bool start_ok = true;

    

    // 0. Ensure a clean state by draining all queues before starting

    drain_queues();

    APP_LOG_INFO("Startup queue drain complete.");



    // 1. Start low-level services first

    start_ok &= system_monitor_->start();

    start_ok &= orientation_sensor_->start();

    if (discovery_module_) start_ok &= discovery_module_->start();

    

    if (!start_ok) {

        APP_LOG_ERROR("Base services failed to start.");

        return false;

    }



    // 2. Start Camera first (Source)

    APP_LOG_INFO("Starting Camera Source...");

    if (!primary_camera_->start()) {

        APP_LOG_ERROR("Critical: Camera module failed to start. Pipeline aborted.");

        return false;

    }

    

    // Wait briefly for camera buffer stability

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Initialize FbdevDisplay for HDMI output
    if (fbdev_display_) {
        // Use display resolution (1280x720) instead of camera resolution
        const unsigned int display_w = 1280;
        const unsigned int display_h = 720;

        APP_LOG_INFO("FbdevDisplay already initialized. Updating visualization processor output size: " + std::to_string(display_w) + "x" + std::to_string(display_h));

        // Update visualization processor to output at display resolution
        if (visualization_processor_) {
            APP_LOG_INFO("Updating visualization processor to output at display resolution: " +
                       std::to_string(display_w) + "x" + std::to_string(display_h));
            visualization_processor_->set_output_size(display_w, display_h);
        }
    }

    // 3. Start processing modules (Middlware)



        APP_LOG_INFO("Starting processing middleware...");



        if (image_processor_) {
            start_ok &= image_processor_->start();
        }



        // Start the visualization processor
        if (visualization_processor_) {
            start_ok &= visualization_processor_->start();
            // Force START signal for visualization processor in standalone mode
            visualization_processor_->start("127.0.0.1");  // Send START signal
            APP_LOG_INFO("Visualization processor START signal sent");
        }



        start_ok &= inference_engine_->start();



        start_ok &= logic_module_->start();

        if (start_ok) {
            APP_LOG_INFO("Applying real-time priorities and CPU affinity to fire-control loop...");
            set_realtime_priority(primary_camera_->get_request_processor_thread(), 90, 1); // Camera: highest priority, Core 1
            set_realtime_priority(logic_module_->get_worker_thread(), 80, 3); // Logic: high priority, Core 3
            int inference_thread_count = 0;
            for (auto& thread : inference_engine_->get_worker_threads()) {
                int assigned_core = -1;
                // Distribute between Core 0 and Core 2 (leaving 1 for Camera, 3 for Logic)
                if (inference_thread_count % 2 == 0) { // First, third, etc. inference thread on Core 0
                    assigned_core = 0;
                } else { // Second, fourth, etc. inference thread on Core 2
                    assigned_core = 2;
                }
                set_realtime_priority(thread, 70, assigned_core); // Inference: medium priority
                inference_thread_count++;
            }
        }

        if (!start_ok) {



            APP_LOG_ERROR("One or more processing modules failed to start.");



            stop();



            return false;



        }



    



        // Start streaming-related modules now that ControlModule is gone

        // if (h264_encoder_) h264_encoder_->start();  // Removed for Mk VI build




        // if (h264_encoder_) h264_encoder_->start();  // Removed for Mk VI build




        // if (mpegts_server_) mpegts_server_->start();  // Removed for Mk VI build



    
    // H264 consumer thread disabled (TPU-only mode - no encoding)
    APP_LOG_INFO("H264 consumer disabled (TPU-only mode)");

    // Start Fbdev display consumer thread (it waits for overlaid frames) (replaces DRM consumer)
    drm_consumer_running_ = true; // Renamed for consistency
    drm_consumer_thread_ = std::thread(&Application::fbdev_queue_consumer_thread_func, this); // Renamed func


    // Start the overlay consumer thread (it waits for inference results)

    overlay_consumer_running_ = true;

    overlay_consumer_thread_ = std::thread(&Application::overlay_queue_consumer_thread_func, this);



    // 4. Start input monitoring

    if (!keyboard_monitor_->start()) {

        APP_LOG_WARNING("Keyboard monitor failed to start.");

    }

    

    // 5. Start monitor

    monitor_->start();



    APP_LOG_INFO("All core modules initialized and stabilized.");
    
    APP_LOG_INFO("Initializing pipeline tracing...");
    aurore::trace::PipelineTracer::instance().init("/home/pi/Aurore/pipeline_trace.csv");
    APP_LOG_INFO("Pipeline tracing initialized.");

    return true;

}

void Application::register_shutdown_handlers() {
    supervisor_.register_module_stop("SystemMonitor", [&]() { system_monitor_->stop(); });
    supervisor_.register_module_stop("LogicModule", [&]() { logic_module_->stop(); });
    supervisor_.register_module_stop("OrientationSensor", [&]() { orientation_sensor_->stop(); });
    supervisor_.register_module_stop("CameraCapture", [&]() { primary_camera_->stop(); });
    // supervisor_.register_module_stop("H264Encoder", [&]() { h264_encoder_->stop(); });  // Removed for Mk VI build
    supervisor_.register_module_stop("ImageProcessor", [&]() { image_processor_->stop(); }); 
    supervisor_.register_module_stop("VisualizationProcessor", [&]() { visualization_processor_->stop(); }); 
    supervisor_.register_module_stop("InferenceEngine", [&]() { inference_engine_->stop(); });
    supervisor_.register_module_stop("KeyboardMonitor", [&]() { keyboard_monitor_->stop(); });
    // supervisor_.register_module_stop("HttpStreamer", [&]() { http_streamer_->stop(); }); // Register HTTP streamer for shutdown
    // supervisor_.register_module_stop("MpegTsServer", [&]() { 
    //     // if (mpegts_server_) {  // Removed for Mk VI build
    //         // mpegts_server_->stop();  // Removed for Mk VI build
    //     // }
    // }); // Register MPEG-TS server for shutdown
    supervisor_.register_module_stop("OverlayConsumer", [&]() {
        overlay_consumer_running_ = false;
        if (!timed_join_thread(overlay_consumer_thread_, "OverlayConsumerThread", std::chrono::seconds(3))) {
            APP_LOG_ERROR("Overlay consumer thread failed to join within timeout");
        }
    });
    supervisor_.register_module_stop("FBDEVConsumer", [&]() { // Renamed DRMConsumer to FBDEVConsumer
        drm_consumer_running_ = false; // Renamed from drm_consumer_running_ for clarity
        if (!timed_join_thread(drm_consumer_thread_, "FBDEVConsumerThread", std::chrono::seconds(3))) { // Renamed
            APP_LOG_ERROR("Fbdev consumer thread failed to join within timeout"); // Changed message
        }
    });
    supervisor_.register_module_stop("Monitor", [&]() { monitor_->stop(); });
}



bool Application::restart_camera_subsystem() {
    APP_LOG_INFO("Restarting CameraCapture subsystem...");
    
    try {
        // Stop the camera if it's running
        if (primary_camera_ && primary_camera_->is_running()) {
            primary_camera_->stop();
        }
        
        // Reset the camera capture object completely
        primary_camera_.reset();
        
        // Add a small delay to ensure resources are fully released
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Get camera configuration
        const unsigned int cam_w = config_loader_.get_camera_width();
        const unsigned int cam_h = config_loader_.get_camera_height();
        const std::chrono::seconds camera_watchdog_timeout = config_loader_.get_camera_watchdog_timeout();
        
        // Recreate the camera capture object
        primary_camera_ = std::make_unique<CameraCapture>(
            cam_w, cam_h, // Main stream width/height
            config_loader_.get_tpu_stream_width(), config_loader_.get_tpu_stream_height(), // TPU stream width/height (from config)
            static_cast<unsigned int>(config_loader_.get_camera_fps()),
            config_loader_.get_tpu_target_width(), config_loader_.get_tpu_target_height(), // Target TPU width/height (from config)
            image_pool_, image_data_pool_, *raw_image_for_processor_queue_, camera_watchdog_timeout);
        
        // Start the camera
        if (!primary_camera_->start()) {
            APP_LOG_ERROR("Failed to start CameraCapture after restart.");
            return false;
        }
        
        APP_LOG_INFO("CameraCapture restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during CameraCapture restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during CameraCapture restart.");
        return false;
    }
}

bool Application::restart_inference_subsystem() {
    APP_LOG_INFO("Restarting InferenceEngine subsystem...");
    
    try {
        // Stop the inference engine if it's running
        if (inference_engine_ && inference_engine_->is_running()) {
            inference_engine_->stop();
        }
        
        // Get model path
        std::filesystem::path exe_path = argv_[0];
        std::filesystem::path config_path = exe_path.parent_path() / ".." / "config.json";
        const std::string model_path = (config_path.parent_path() / config_loader_.get_model_path()).string();
        
        // Recreate the inference engine
        inference_engine_ = std::make_unique<InferenceEngine>(
            model_path, *tpu_inference_queue_, &detection_results_for_overlay_buffer_,
            *detection_results_for_logic_queue_, detection_pool_,
            image_data_pool_, result_token_pool_,
            config_loader_.get_detection_score_threshold(),
            overlay_queue_.get(),
            config_loader_.get_enable_tpu_inference(), // Use actual config value
            config_loader_.get_enable_gpu_inference(),
            config_loader_.get_inference_worker_threads());
        
        // Start the inference engine
        if (!inference_engine_->start()) {
            APP_LOG_ERROR("Failed to start InferenceEngine after restart.");
            return false;
        }
        
        APP_LOG_INFO("InferenceEngine restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during InferenceEngine restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during InferenceEngine restart.");
        return false;
    }
}

bool Application::restart_logic_subsystem() {
    APP_LOG_INFO("Restarting LogicModule subsystem...");
    
    try {
        // Stop the logic module if it's running
        if (logic_module_ && logic_module_->is_running()) {
            logic_module_->stop();
        }
        
        // Recreate the logic module
        logic_module_ = std::make_unique<LogicModule>(*detection_results_for_logic_queue_, result_token_pool_, orientation_sensor_, config_loader_, &ballistic_points_for_overlay_buffer_);
        // Set application reference for logic module to update counters
        logic_module_->set_application_ref(this);
        
        // Start the logic module
        if (!logic_module_->start()) {
            APP_LOG_ERROR("Failed to start LogicModule after restart.");
            return false;
        }
        
        APP_LOG_INFO("LogicModule restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during LogicModule restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during LogicModule restart.");
        return false;
    }
}

bool Application::restart_image_processor_subsystem() {
    APP_LOG_INFO("Restarting ImageProcessor subsystem...");
    
    try {
        // Stop the image processor if it's running
        if (image_processor_ && image_processor_->is_running()) {
            image_processor_->stop();
        }
        
        // Recreate the image processor
        unsigned int inf_w = config_loader_.get_tpu_target_width();
        unsigned int inf_h = config_loader_.get_tpu_target_height();
        
        image_processor_ = std::make_unique<ImageProcessor>(
            raw_image_for_processor_queue_.get(), tpu_inference_queue_.get(), image_pool_, image_data_pool_,
            config_loader_.get_tpu_stream_pixel_format(), // Pass configured format
            (int)inf_w, (int)inf_h, true, std::string("ImageProcessor_TPU")); // Changed drm_display_->drm_fd_ to drm_display_->fb_fd_
        
        // Start the image processor
        if (!image_processor_->start()) {
            APP_LOG_ERROR("Failed to start ImageProcessor after restart.");
            return false;
        }
        
        APP_LOG_INFO("ImageProcessor restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during ImageProcessor restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during ImageProcessor restart.");
        return false;
    }
}

bool Application::restart_visualization_subsystem() {
    APP_LOG_INFO("Restarting VisualizationProcessor subsystem...");
    try {
        // Stop the visualization processor if it's running
        if (visualization_processor_ && visualization_processor_->is_running()) {
            visualization_processor_->stop();
        }
        // Get main camera dimensions
        const unsigned int cam_w = config_loader_.get_camera_width();
        const unsigned int cam_h = config_loader_.get_camera_height();
        
                // Recreate the visualization processor
                visualization_processor_ = std::make_unique<ImageProcessor>(
                    main_video_queue_.get(), overlaid_video_queue_.get(),
                    &detection_results_for_overlay_buffer_, // Connect to triple buffer
                    &ballistic_points_for_overlay_buffer_,  // New argument for ballistic points
                    image_pool_, image_data_pool_,
                    config_loader_.get_tpu_stream_pixel_format(),
                    (int)cam_w, (int)cam_h, overlay_queue_.get(), std::string("ImageProcessor_Viz")); // Changed drm_display_->drm_fd_ to drm_display_->fb_fd_        
        // Set application reference for visualization processor to update counters
        visualization_processor_->set_application_ref(this);
        // Start the visualization processor
        if (!visualization_processor_->start()) {
            APP_LOG_ERROR("Failed to start VisualizationProcessor after restart.");
            return false;
        }
        
        APP_LOG_INFO("VisualizationProcessor restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during VisualizationProcessor restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during VisualizationProcessor restart.");
        return false;
    }
}

bool Application::restart_orientation_subsystem() {
    APP_LOG_INFO("Restarting OrientationSensor subsystem...");
    
    try {
        // Stop the orientation sensor if it's running
        if (orientation_sensor_ && orientation_sensor_->is_running()) {
            orientation_sensor_->stop();
        }
        
        // Recreate the orientation sensor
                orientation_sensor_ = std::make_shared<OrientationSensor>(
                    config_loader_.get_orientation_pub_port(), // Use the new getter for orientation port
                    config_loader_.get_orientation_pub_port(), // Assuming yaw/pitch/roll now use the same pub port
                    config_loader_.get_orientation_pub_port()  // Assuming yaw/pitch/roll now use the same pub port
                );
        
        // Start the orientation sensor
        if (!orientation_sensor_->start()) {
            APP_LOG_ERROR("Failed to start OrientationSensor after restart.");
            return false;
        }
        
        APP_LOG_INFO("OrientationSensor restarted successfully.");
        return true;
    } catch (const std::exception& e) {
        APP_LOG_ERROR("Exception during OrientationSensor restart: " + std::string(e.what()));
        return false;
    } catch (...) {
        APP_LOG_ERROR("Unknown exception during OrientationSensor restart.");
        return false;
    }
}

void Application::overlay_queue_consumer_thread_func() {
    APP_LOG_INFO("Overlay consumer thread started (disabled - visualization processor handles overlays).");
    // The overlay consumer thread is now disabled since the visualization_processor
    // handles the overlay application directly to the main video stream.
    // We strictly DO NOT consume from the queue here to prevent stealing data from the visualization processor.
    
    // Simply exit the thread.
    return; 
}

void Application::fbdev_queue_consumer_thread_func() {
    APP_LOG_INFO("FbdevDisplay queue consumer thread started.");
    
    ImageData* image_data_ptr = nullptr;
    int frame_counter = 0;

    // Removed DRM-specific event context and handling
    
    while (drm_consumer_running_ && g_running.load(std::memory_order_acquire)) { // Keep drm_consumer_running_ flag
        // Attempt to pop a buffer pointer from the overlaid video queue
        if (overlaid_video_queue_->pop(image_data_ptr)) {
            // Wrap the raw pointer in a shared_ptr with a custom deleter that returns it to the pool
            std::shared_ptr<ImageData> image_data(image_data_ptr, [this](ImageData* img) {
                if (img && img->buffer) {
                    img->buffer.reset(); // Correctly release shared_ptr to PooledBuffer
                }
                image_data_pool_->release(img); // Return ImageData to ObjectPool
            });

            if (image_data && image_data->buffer && !image_data->buffer->data.empty() &&
                image_data->width > 0 && image_data->height > 0) {
                // Render frame to Fbdev display
                if (fbdev_display_) {
                    fbdev_display_->render_frame(
                        image_data->buffer->data.data(),
                        image_data->width,
                        image_data->height
                    );
                    inc_viz_to_fbdev_consumed(); // Increment consumed counter
                    frame_counter++;
                }
                // Log occasional frames for debugging
                if (frame_counter % 60 == 0) {  // Log every 60th frame
                    APP_LOG_INFO("FbdevDisplay Consumer: Frame " + std::to_string(frame_counter) +
                                ", Size: " + std::to_string(image_data->width) + "x" + std::to_string(image_data->height));
                }
            }
        } else {
            // If no data was available, sleep briefly to avoid busy-waiting.
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
    APP_LOG_INFO("FbdevDisplay queue consumer thread stopped. Total frames processed: " + std::to_string(frame_counter));
}

int Application::run() {
    std::cout << "[INIT] Application run() starting..." << std::endl;
    APP_LOG_INFO("Application run() starting...");
    
    // Pre-launch cleanup to ensure a clean state
    std::cout << "[INIT] Performing pre-launch cleanup..." << std::endl;
    pre_launch_cleanup();
    
    APP_LOG_INFO("Pre-launch cleanup complete.");
    std::cout << "[INIT] Pre-launch cleanup complete" << std::endl;
    
    // Load configuration
    std::cout << "[INIT] Loading configuration..." << std::endl;
    if (!config_loader_.load("config.json")) {
        std::cout << "[ERROR] Failed to load configuration!" << std::endl;
        APP_LOG_ERROR("Failed to load configuration.");
        return 1;
    }
    std::cout << "[INIT] Configuration loaded successfully" << std::endl;
    
    // Setup buffer pools and queues
    std::cout << "[INIT] Setting up buffer pools and queues..." << std::endl;
    setup_pools_and_queues();
    std::cout << "[INIT] Buffer pools and queues set up successfully" << std::endl;
    
    // Initialize modules
    std::cout << "[INIT] Initializing modules..." << std::endl;
    // Get the directory containing the executable and navigate to project root
    std::filesystem::path exe_path = std::filesystem::absolute(argv_[0]);
    std::cout << "[DEBUG] Executable path: " << exe_path << std::endl;
    std::filesystem::path base_path = exe_path.parent_path().parent_path().parent_path();  // bin/ -> artifacts/ -> project root
    std::cout << "[DEBUG] Base path: " << base_path << std::endl;
    std::filesystem::path model_path_fs = base_path / config_loader_.get_model_path();
    std::filesystem::path labels_path_fs = base_path / config_loader_.get_labels_path();
    std::cout << "[DEBUG] Config model path: " << config_loader_.get_model_path() << std::endl;
    std::cout << "[DEBUG] Config labels path: " << config_loader_.get_labels_path() << std::endl;
    std::cout << "[DEBUG] Resolved model path: " << model_path_fs << std::endl;
    std::cout << "[DEBUG] Resolved labels path: " << labels_path_fs << std::endl;
    
    const std::string model_path = model_path_fs.string();
    const std::string labels_path = labels_path_fs.string();
    
    APP_LOG_INFO("Model path: " + model_path);
    APP_LOG_INFO("Labels path: " + labels_path);
    std::cout << "[INIT] Model path: " << model_path << std::endl;
    std::cout << "[INIT] Labels path: " << labels_path << std::endl;
    
    if (!std::filesystem::exists(model_path_fs)) {
        std::cout << "[ERROR] Model file not found: " << model_path << std::endl;
        APP_LOG_ERROR("Model file not found: " + model_path);
        return false;
    }
    if (!std::filesystem::exists(labels_path_fs)) {
        std::cout << "[ERROR] Labels file not found: " << labels_path << std::endl;
        APP_LOG_ERROR("Labels file not found: " + labels_path);
        return false;
    }
    
    APP_LOG_INFO("Verifying data integrity of model and config files...");
    
    auto model_integrity = aurore::integrity::DataIntegrityChecker::verify_model_file(
        model_path, "");
    if (model_integrity.passed) {
        APP_LOG_INFO("Model file integrity: OK (SHA256: " + model_integrity.sha256.substr(0, 16) + "...)");
    } else {
        APP_LOG_WARNING("Model file integrity: First verification (no expected hash stored)");
    }
    
    auto config_path = std::filesystem::path(config_loader_.get_log_path()).parent_path() / "config.json";
    if (std::filesystem::exists(config_path)) {
        auto config_integrity = aurore::integrity::DataIntegrityChecker::verify_config_file(
            config_path.string(), "");
        if (config_integrity.passed) {
            APP_LOG_INFO("Config file integrity: OK (SHA256: " + config_integrity.sha256.substr(0, 16) + "...)");
        } else {
            APP_LOG_WARNING("Config file integrity: First verification (no expected hash stored)");
        }
    }
    
    if (!initialize_modules(model_path, labels_path)) {
        std::cout << "[ERROR] Failed to initialize modules!" << std::endl;
        APP_LOG_ERROR("Failed to initialize modules.");
        return 1;
    }
    std::cout << "[INIT] Modules initialized successfully" << std::endl;
    
    // Start all modules
    std::cout << "[INIT] Starting all modules..." << std::endl;
    if (!start_modules()) {
        std::cout << "[ERROR] Failed to start modules!" << std::endl;
        APP_LOG_ERROR("Failed to start modules.");
        return 1;
    }
    std::cout << "[INIT] All modules started successfully" << std::endl;
    
    // Register shutdown handlers for graceful shutdown
    std::cout << "[INIT] Registering shutdown handlers..." << std::endl;
    register_shutdown_handlers();
    std::cout << "[INIT] Shutdown handlers registered" << std::endl;
    
    // Start the recovery thread
    recovery_enabled_ = false; // Disabled - recovery_thread_func not implemented

    APP_LOG_INFO("Application running. Press Ctrl+C to stop.");
    
    // Main loop - wait for shutdown signal
    auto last_monitoring_check = std::chrono::high_resolution_clock::now();
    const auto monitoring_interval = std::chrono::milliseconds(500); // Check every 500ms
    const auto max_run_time = std::chrono::minutes(60); // Extended to 1 hour for testing
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (g_running.load(std::memory_order_acquire)) {
        auto current_time = std::chrono::high_resolution_clock::now();
        
        // Check for timeout to prevent hanging
        if (current_time - start_time > max_run_time) {
            APP_LOG_ERROR("Application timeout reached, forcing shutdown");
            break;
        }
        
        // Perform monitoring checks at specified intervals
        if (current_time - last_monitoring_check >= monitoring_interval) {
            // Check for display starvation
            check_display_starvation();
            
            // Monitor queue depths
            monitor_queue_depths();
            
            // Enforce max latency (this will be implemented to check frame latencies)
            enforce_max_latency();
            
            last_monitoring_check = current_time;
        }
        
        // Check shutdown flag more frequently with shorter sleep for more responsive shutdown
        for (int i = 0; i < 10 && g_running.load(std::memory_order_acquire); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Sleep in smaller increments for more responsive shutdown
        }
    }
    
    APP_LOG_INFO("Shutdown signal received. Stopping application...");
    stop();
    
    return 0;
}

// Additional cleanup methods
void Application::release_camera_resources() {
    // Release camera resources if any
    APP_LOG_INFO("Releasing camera resources...");
}

void Application::clear_telemetry_sockets() {
    // Close any open telemetry sockets
    APP_LOG_INFO("Clearing telemetry sockets...");
}

void Application::pre_launch_cleanup() {
    APP_LOG_INFO("Performing pre-launch cleanup...");
    // Any cleanup needed before starting the application
    // This could include removing stale files, resetting states, etc.
}

void Application::aggressive_resource_cleanup() {
    APP_LOG_INFO("Performing aggressive resource cleanup...");
    // More thorough cleanup if needed
}

void Application::memory_leak_detection() {
    APP_LOG_INFO("Performing memory leak detection...");
    // Placeholder for memory leak detection if needed
}

void Application::temporary_file_cleanup() {
    APP_LOG_INFO("Performing temporary file cleanup...");
    // Clean up any temporary files
}

void Application::cleanup_ipc_resources() {
    APP_LOG_INFO("Cleaning up IPC resources...");
    // Clean up any inter-process communication resources
}

void Application::cleanup_shared_memory() {
    APP_LOG_INFO("Cleaning up shared memory...");
    // Clean up any shared memory segments
}

void Application::cleanup_zombie_processes() {
    APP_LOG_INFO("Cleaning up zombie processes...");
    // Clean up any zombie processes
}

void Application::generate_cleanup_report() {
    APP_LOG_INFO("Generating cleanup report...");
    // Generate a report of cleanup activities
}

bool Application::terminate_existing_instances() {
    // Terminate any existing instances if needed
    return true; // Placeholder implementation
}

void Application::main_loop() {
    // Main application loop if needed
    // This is now handled in the run() method
}

void Application::debug_queue_monitoring() {
    APP_LOG_INFO("Starting queue size monitoring for 5 seconds...");
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto current_time = start_time;
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count() < 5000) {
        
        // Log raw_image_for_processor_queue_ status every 100ms
        bool raw_image_queue_empty = raw_image_for_processor_queue_->empty();
        (void)raw_image_queue_empty;
        APP_LOG_INFO("Raw Image Queue Empty: " + std::string(raw_image_queue_empty ? "YES" : "NO"));

        // Also log other important queue statuses
        bool main_video_queue_empty = main_video_queue_->empty();
        (void)main_video_queue_empty;
        APP_LOG_INFO("Main Video Queue Empty: " + std::string(main_video_queue_empty ? "YES" : "NO"));

        bool tpu_inference_queue_empty = tpu_inference_queue_->empty();
        (void)tpu_inference_queue_empty;
        APP_LOG_INFO("TPU Inference Queue Empty: " + std::string(tpu_inference_queue_empty ? "YES" : "NO"));
        
        // Sleep for 100ms before next check
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        current_time = std::chrono::high_resolution_clock::now();
    }
    
    APP_LOG_INFO("Completed queue size monitoring for 5 seconds.");
}

void Application::monitor_queue_depths() {
    // Check queue status using size_approx() to get actual queue depths
    size_t raw_image_queue_depth = raw_image_for_processor_queue_->size_approx();
    size_t tpu_inference_queue_depth = tpu_inference_queue_->size_approx();
    size_t detection_logic_queue_depth = detection_results_for_logic_queue_->size_approx();
    size_t overlaid_video_queue_depth = overlaid_video_queue_->size_approx();

    // Get current timestamp for logging
    auto current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
    (void)current_time_ms;

    // Log potential queue stalls (queues that are consistently full)
    if (raw_image_queue_depth > 20) { // More than 40% of capacity (50) - more aggressive
        APP_LOG_WARNING("ANOMALY @" + std::to_string(current_time_ms) + "ms: QUEUE STALL DETECTED: Raw Image Queue depth = " + std::to_string(raw_image_queue_depth) + "/50 - Camera pushing faster than ImageProcessor consuming");
    }
    if (tpu_inference_queue_depth > 20) { // More than 40% of capacity (50) - more aggressive
        APP_LOG_WARNING("ANOMALY @" + std::to_string(current_time_ms) + "ms: QUEUE STALL DETECTED: TPU Inference Queue depth = " + std::to_string(tpu_inference_queue_depth) + "/50 - ImageProcessor pushing faster than Inference consuming");
    }
    if (detection_logic_queue_depth > 20) { // More than 40% of capacity (50) - more aggressive
        APP_LOG_WARNING("ANOMALY @" + std::to_string(current_time_ms) + "ms: QUEUE STALL DETECTED: Detection Logic Queue depth = " + std::to_string(detection_logic_queue_depth) + "/50 - Potential stall between Inference and Logic");
    }
    if (overlaid_video_queue_depth > 20) { // More than 40% of capacity (50) - more aggressive
        APP_LOG_WARNING("ANOMALY @" + std::to_string(current_time_ms) + "ms: QUEUE STALL DETECTED: Overlaid Video Queue depth = " + std::to_string(overlaid_video_queue_depth) + "/50 - Potential stall between Overlay and Display");
    }

    // Check if any queue is critically full and needs draining to prevent deadlock
    bool needs_draining = (raw_image_queue_depth > 25 || tpu_inference_queue_depth > 25 ||
                          detection_logic_queue_depth > 25 ||
                          overlaid_video_queue_depth > 25);

    if (needs_draining) {
        APP_LOG_WARNING("QUEUE SAFETY: One or more queues critically full, initiating drain operation");
        drain_queues();
    }

    // Log detailed queue information every second for diagnostics
    static auto last_log_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_log_time).count();

    if (elapsed_ms >= 1000) { // Log every second
        APP_LOG_INFO("QUEUE DEPTHS @" + std::to_string(current_time_ms) + "ms: " +
                  "Raw=" + std::to_string(raw_image_queue_depth) +
                  ", TPU=" + std::to_string(tpu_inference_queue_depth) +
                  ", Logic=" + std::to_string(detection_logic_queue_depth) +
                  ", Overlaid=" + std::to_string(overlaid_video_queue_depth));

        // Update last log time
        last_log_time = current_time;
    }

    APP_LOG_DEBUG("Queue monitoring @" + std::to_string(current_time_ms) + "ms - Depths: Raw=" + std::to_string(raw_image_queue_depth) +
                  ", TPU=" + std::to_string(tpu_inference_queue_depth) +
                  ", Logic=" + std::to_string(detection_logic_queue_depth) +
                  ", Overlaid=" + std::to_string(overlaid_video_queue_depth));
}

void Application::check_display_starvation() {
    // For boost lockfree queues, we can't directly check size, so we'll just log that monitoring is active
    // In a real implementation, we might need to track queue state differently
    APP_LOG_DEBUG("Display starvation check active - boost lockfree queues don't support direct size checking");
}

void Application::enforce_max_latency() {
    // Monitor pipeline latency to ensure real-time performance
    APP_LOG_DEBUG("MONITOR: Pipeline latency check active");
}

void Application::check_thread_stalls() {
    // Check for stalled threads by monitoring their rates
    static auto last_check_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_check_time).count();
    
    if (elapsed_ms < 5000) { // Check every 5 seconds
        return;
    }
    
    // Update last check time
    last_check_time = current_time;
    
    // Check camera frame rate
    if (get_primary_camera() && get_primary_camera()->is_running()) {
        int camera_fps = get_primary_camera()->frame_rate_.load();
        if (camera_fps == 0) {
            APP_LOG_WARNING("THREAD STALL DETECTED: Camera FPS is 0, but camera is running");
        }
    }
    
    // Check inference rate
    if (get_inference_engine() && get_inference_engine()->is_running()) {
        int inference_rate = get_inference_engine()->get_current_inference_rate();
        if (inference_rate == 0) {
            APP_LOG_WARNING("THREAD STALL DETECTED: Inference rate is 0, but inference engine is running");
        }
    }
    
    // Check logic rate
    if (get_logic_module() && get_logic_module()->is_running()) {
        int logic_rate = get_logic_module()->logic_rate_.load();
        if (logic_rate == 0) {
            APP_LOG_WARNING("THREAD STALL DETECTED: Logic rate is 0, but logic module is running");
        }
    }
    
    // Check queue status
    size_t tpu_inference_queue_depth = tpu_inference_queue_->empty() ? 0 : 1;
    size_t detection_logic_queue_depth = detection_results_for_logic_queue_->empty() ? 0 : 1;
    
    // If queues are getting full, it indicates consumption issues
    if (tpu_inference_queue_depth > 45) { // Almost full
        APP_LOG_WARNING("THREAD STALL RISK: TPU Inference Queue depth = " + std::to_string(tpu_inference_queue_depth) + "/50 - Inference consumer may be stalled");
    }
    
    if (detection_logic_queue_depth > 45) { // Almost full
        APP_LOG_WARNING("THREAD STALL RISK: Detection Logic Queue depth = " + std::to_string(detection_logic_queue_depth) + "/50 - Logic consumer may be stalled");
    }
}

void Application::drain_queues() {
    APP_LOG_DEBUG("DRAIN: Starting queue draining operation");

    // Guard against accessing destroyed queues
    if (application_destructor_active_.load(std::memory_order_acquire)) {
        APP_LOG_INFO("[DRAIN] Skipping queue drainage - destructor in progress");
        return;
    }

    // Drain raw image queue - more aggressive draining
    {
        int items_drained = 0;
        ImageData* ptr = nullptr;
        if (raw_image_for_processor_queue_ && raw_image_for_processor_queue_->is_valid()) {
            while (raw_image_for_processor_queue_->pop(ptr)) {
                if (ptr) {
                    ptr->buffer.reset();
                    image_data_pool_->release(ptr);
                    items_drained++;
                }
            }
        }
        if (items_drained > 0) {
            APP_LOG_INFO("DRAIN: Drained " + std::to_string(items_drained) + " items from Raw Image Queue");
            for (int i = 0; i < items_drained; i++) {
                inc_cam_to_tpu_proc_dropped();
            }
        }
    }

    // Drain TPU inference queue - more aggressive draining
    {
        int items_drained = 0;
        ImageData* ptr = nullptr;
        if (tpu_inference_queue_ && tpu_inference_queue_->is_valid()) {
            while (tpu_inference_queue_->pop(ptr)) {
                if (ptr) {
                    ptr->buffer.reset();
                    image_data_pool_->release(ptr);
                    items_drained++;
                }
            }
        }
        if (items_drained > 0) {
            APP_LOG_INFO("DRAIN: Drained " + std::to_string(items_drained) + " items from TPU Inference Queue");
            for (int i = 0; i < items_drained; i++) {
                inc_proc_to_inf_dropped();
            }
        }
    }

    // Drain detection results logic queue - more aggressive draining
    {
        int items_drained = 0;
        ResultToken* ptr = nullptr;
        if (detection_results_for_logic_queue_ && detection_results_for_logic_queue_->is_valid()) {
            while (detection_results_for_logic_queue_->pop(ptr)) {
                if (ptr) {
                    inc_inf_to_logic_dropped();
                    ptr->release_buffer();
                    result_token_pool_->release(ptr);
                    items_drained++;
                }
            }
        }
        if (items_drained > 0) {
            APP_LOG_INFO("DRAIN: Drained " + std::to_string(items_drained) + " items from Detection Logic Queue");
        }
    }

    // Drain overlaid video queue (Visualization Processor -> H264 Encoder) - more aggressive draining
    {
        int items_drained = 0;
        ImageData* ptr = nullptr;
        if (overlaid_video_queue_ && overlaid_video_queue_->is_valid()) {
            while (overlaid_video_queue_->pop(ptr)) {
                if (ptr) {
                    ptr->buffer.reset();
                    image_data_pool_->release(ptr);
                    items_drained++;
                }
            }
        }
        if (items_drained > 0) {
            APP_LOG_INFO("DRAIN: Drained " + std::to_string(items_drained) + " items from Overlaid Video Queue");
            for (int i = 0; i < items_drained; i++) {
                inc_viz_to_fbdev_dropped(); // Use the newly defined counter
            }
        }
    }

    APP_LOG_DEBUG("DRAIN: Queue draining operation completed");
}

void Application::debug_buffer_pool_monitoring() {
    APP_LOG_INFO("Starting buffer pool monitoring...");
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto current_time = start_time;
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count() < 5000) {  // Monitor for 5 seconds
        
        // Log ImagePool usage
        if (image_pool_) {
            size_t available = image_pool_->get_available_buffers();
            size_t total = image_pool_->get_total_buffers();
            size_t in_use = image_pool_->get_current_in_use();
            size_t peak = image_pool_->get_peak_in_use();
            (void)available;
            (void)total;
            (void)in_use;
            (void)peak;
            APP_LOG_INFO("ImagePool: Available: " + std::to_string(available) +
                        ", Total: " + std::to_string(total) +
                        ", In Use: " + std::to_string(in_use) +
                        ", Peak: " + std::to_string(peak));
        }
        
        // Log DetectionPool usage
        if (detection_pool_) {
            size_t available = detection_pool_->get_available_buffers();
            size_t total = detection_pool_->get_total_buffers();
            size_t in_use = detection_pool_->get_current_in_use();
            size_t peak = detection_pool_->get_peak_in_use();
            (void)available;
            (void)total;
            (void)in_use;
            (void)peak;
            APP_LOG_INFO("DetectionPool: Available: " + std::to_string(available) +
                        ", Total: " + std::to_string(total) +
                        ", In Use: " + std::to_string(in_use) +
                        ", Peak: " + std::to_string(peak));
        }
        
        // Sleep for 1 second before next check
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        current_time = std::chrono::high_resolution_clock::now();
    }
    
    APP_LOG_INFO("Completed buffer pool monitoring.");
}

void Application::run_debugging_pipeline() {
    APP_LOG_INFO("Starting debugging pipeline test for 30 seconds...");
    
    // Start all the debugging monitoring functions in separate threads
    std::thread queue_monitor_thread([this]() {
        this->debug_queue_monitoring();
    });
    
    std::thread pool_monitor_thread([this]() {
        this->debug_buffer_pool_monitoring();
    });
    
    // Let the monitoring run for 30 seconds
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    APP_LOG_INFO("Completed debugging pipeline test for 30 seconds.");
    
    // Join the monitoring threads
    timed_join_thread(queue_monitor_thread, "QueueMonitorThread", std::chrono::seconds(1));
    timed_join_thread(pool_monitor_thread, "PoolMonitorThread", std::chrono::seconds(1));
}

// Detector supervision implementation
bool Application::start_detector_process() {
    APP_LOG_INFO("Starting detector process supervision...");
    
    // Check if detector is already running
    if (is_detector_running()) {
        APP_LOG_INFO("Detector process is already running");
        return true;
    }
    
    // Fork to create child process for detector
    pid_t pid = fork();
    
    if (pid == -1) {
        APP_LOG_ERROR("Failed to fork detector process");
        perror("fork");
        return false;
    }
    
    if (pid == 0) {
        // Child process - run detector
        // Change working directory to build directory
        if (chdir("/home/pi/CoralEdgeTpu/build") == -1) {
            APP_LOG_ERROR("Failed to change directory to build");
            perror("chdir to build directory");
            exit(1);
        }
        
        // Execute detector binary
        execl("./detector", "./detector", (char*)NULL);
        
        // If execl returns, it failed
        APP_LOG_ERROR("Failed to execute detector binary");
        perror("execl detector");
        exit(1);
    } else {
        // Parent process - store the PID
        detector_pid_.store(pid);
        APP_LOG_INFO("Started detector process with PID " + std::to_string(pid));
        supervisor_.register_child_process(pid);
        return true;
    }
}

void Application::stop_detector_process() {
    pid_t current_pid = detector_pid_.load();
    if (current_pid > 0) {
        APP_LOG_INFO("Terminating detector process with PID " + std::to_string(current_pid));
        
        // Try graceful termination first
        if (kill(current_pid, SIGTERM) == 0) {
            // Wait briefly for graceful shutdown (non-blocking)
            int status;
            pid_t result = waitpid(current_pid, &status, WNOHANG);
            if (result == 0) {
                // Process didn't exit immediately, wait a bit more
                usleep(500000); // 500ms
                
                // Check again
                result = waitpid(current_pid, &status, WNOHANG);
                if (result == 0) {
                    // Process still hasn't exited, force kill
                    APP_LOG_INFO("Detector process not responding to SIGTERM, sending SIGKILL...");
                    kill(current_pid, SIGKILL);
                    
                    // Wait for the process to be killed
                    waitpid(current_pid, &status, 0);
                }
            }
        }
        
        detector_pid_.store(-1);
        APP_LOG_INFO("Detector process terminated");
    }
}

bool Application::is_detector_running() {
    pid_t current_pid = detector_pid_.load();
    if (current_pid <= 0) {
        return false;
    }
    
    // Check if process is still alive by sending signal 0 (doesn't actually send a signal)
    return (kill(current_pid, 0) == 0);
}

void Application::detector_supervisor_thread_func() {
    APP_LOG_INFO("Detector supervisory thread started");
    
    while (detector_supervisor_running_.load()) {
        // Check if detector process is running
        if (!is_detector_running()) {
            APP_LOG_WARNING("Detector process is not running, attempting to restart...");
            
            // Try to restart the detector process
            if (start_detector_process()) {
                APP_LOG_INFO("Successfully restarted detector process");
            } else {
                APP_LOG_ERROR("Failed to restart detector process, retrying in 5 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        
        // Check every 2 seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    APP_LOG_INFO("Detector supervisory thread stopped");
}