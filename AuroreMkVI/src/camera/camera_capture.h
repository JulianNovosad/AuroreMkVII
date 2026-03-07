// Verified headers: [thread, atomic, vector, string, chrono...]
// Verification timestamp: 2026-01-06 17:08:04
// REMEDIATION 2026-02-02: Added explicit memory ordering for atomics
#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <libcamera/libcamera.h>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/framebuffer_allocator.h>
#include <libcamera/stream.h>
#include <libcamera/request.h>
#include <libcamera/geometry.h>
#include <libcamera/pixel_format.h>

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <list>
#include <memory>
#include <functional>
#include <map> // Explicitly include map for mapped_buffers_

#include <opencv2/opencv.hpp>

#include "pipeline_structs.h"
#include "buffer_pool.h"
#include "atomic_ordering.h"

// MappedBufferInfo struct for DMA-BUF information
struct MappedBufferInfo {
    void* addr;
    size_t length;
    int fd;       // DMA-BUF file descriptor
    size_t offset; // Offset within DMA-BUF
};

/**
 * @file camera_capture.h
 * @brief Manages the video capture pipeline with libcamera.
 *
 * This class is responsible for initializing the camera,
 * configuring video streams (for display and inference),
 * and capturing frames in a multi-threaded environment.
 */
class CameraCapture {
public:
    /**
     * @brief Constructor for the CameraCapture class.
     * @param main_width Width of the high-resolution video stream.
     * @param main_height Height of the high-resolution video stream.
     * @param tpu_width Width of the low-resolution stream for the TPU.
     * @param tpu_height Height of the low-resolution stream for the TPU.
     * @param tpu_fps Frame rate for the TPU stream.
     * @param target_tpu_width Target width for TPU inference.
     * @param target_tpu_height Target height for TPU inference.
     * @param image_buffer_pool A shared pool for managing image buffers.
     * @param image_data_pool A shared pool for ImageData objects.
     * @param image_processor_input_queue A queue for raw frames for the ImageProcessor.
     * @param watchdog_timeout Timeout for the camera watchdog.
     */
    CameraCapture(unsigned int main_width, unsigned int main_height,
                                                                            unsigned int tpu_width, unsigned int tpu_height,
                                                                            unsigned int tpu_fps,
                                                                            unsigned int target_tpu_width, unsigned int target_tpu_height,
                                                                            std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool,
                                                                            std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                                                                            ImageQueue& image_processor_input_queue,
                                                                            std::chrono::seconds watchdog_timeout);
                                               
    void set_main_output_queues(const std::vector<ImageQueue*>& queues) { main_output_queues_ = queues; }
    ~CameraCapture();

    /**
     * @brief Starts the video capture thread.
     * @return True if starting was successful, false otherwise.
     */
    bool start();

    /**
     * @brief Stopt de video-opname thread.
     */
    void stop();

    /**
     * @brief Controleert of de opname-engine draait.
     * @return True als de engine draait, anders false.
     */
    bool is_running() const { return running_; }

    /**
     * @brief Logt de huidige status van de camera-instellingen.
     */
    void get_state() const;

    /**
     * @brief Configureert de camera met de opgegeven stream-instellingen.
     * @return True als de configuratie succesvol was, anders false.
     */
    bool setup_camera();

    /**
     * @brief Callback-functie die wordt aangeroepen wanneer een frame-request is voltooid.
     * @param request The completed libcamera request.
     */
    void request_complete_callback(libcamera::Request* request);

    /**
     * @brief Accepteert en configureert de eerste beschikbare camera.
     * @return True als een camera succesvol is geconfigureerd, false anders.
     */
    bool acquire_camera();

    /**
     * @brief Stelt een callback in voor het toevoegen van overlays op de frames.
     * @param callback The function called with a cv::Mat frame as argument.
     */
    bool init_video_encoder();
    void set_overlay_callback(std::function<void(cv::Mat& frame)> callback) {
        overlay_callback_ = callback;
    }
    
    // Timing methods for monitoring
    long long get_capture_timing_us() const { return Aurore::atomic_load_acquire(avg_capture_time_us_); }
    long long get_total_loop_timing_us() const { return Aurore::atomic_load_acquire(avg_total_loop_time_us_); }

    // Freshness indicators
    std::atomic<long long> last_frame_timestamp_{0}; ///< Timestamp of the last processed frame
    std::atomic<int> frame_rate_{0}; ///< Current frame rate
    
    // Drop counters for proper queue accounting
    std::atomic<int64_t> main_stream_drop_count_{0}; ///< Count of frames dropped from main stream queue
    std::atomic<int64_t> tpu_stream_drop_count_{0};  ///< Count of frames dropped from TPU stream queue
    
    // Public getters for drop counters to be used by Monitor
    int64_t get_main_stream_drop_count() const { return Aurore::atomic_load_acquire(main_stream_drop_count_); }
    int64_t get_tpu_stream_drop_count() const { return Aurore::atomic_load_acquire(tpu_stream_drop_count_); }
    
    // Method to allow application to increment drop counters when draining queues
    void increment_main_stream_drop_count() { main_stream_drop_count_.fetch_add(1, std::memory_order_relaxed); }
    void increment_tpu_stream_drop_count() { tpu_stream_drop_count_.fetch_add(1, std::memory_order_relaxed); }
    
    // Public getters for frame accounting counters
    int64_t get_frames_produced() const { return Aurore::atomic_load_acquire(frames_produced_); }
    int64_t get_frames_consumed_by_inference() const { return Aurore::atomic_load_acquire(frames_consumed_by_inference_); }
    
    // Method to set application reference for updating counters
    void set_application_ref(class Application* app) { app_ref_ = app; }

    // Method to set direct inference output queue when TPU is disabled
    void set_direct_inference_output_queue(ImageQueue* queue) { direct_inference_queue_ = queue; }

    std::thread& get_request_processor_thread() { return request_processor_thread_; }

private: // Single private block for all private members
    // Timing statistics
    mutable std::atomic<long long> avg_capture_time_us_{0};
    mutable std::atomic<long long> avg_total_loop_time_us_{0};
    
    std::atomic<bool> running_ = false; ///< Flag to manage the state of the worker thread.
    std::atomic<bool> processing_running_ = false;

    unsigned int width_; ///< Width of the main stream.
    unsigned int height_; ///< Height of the main stream.
    unsigned int tpu_width_; ///< Width of the TPU stream.
    unsigned int tpu_height_; ///< Height of the TPU stream.
    unsigned int tpu_fps_; ///< Frame rate for the TPU stream.

    unsigned int target_tpu_width_; ///< Target width for TPU inference after resizing.
    unsigned int target_tpu_height_; ///< Target height for TPU inference after resizing.

    ImageQueue& image_processor_input_queue_;  ///< Queue for raw frames for the ImageProcessor.
    std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool_; ///< Pool for managing image buffers.
    std::shared_ptr<ObjectPool<ImageData>> image_data_pool_; ///< Pool for ImageData objects.
    std::chrono::seconds watchdog_timeout_; ///< Timeout for the camera watchdog.

    std::unique_ptr<libcamera::CameraManager> camera_manager_; ///< Manages available cameras.
    std::shared_ptr<libcamera::Camera> camera_; ///< The selected camera.
    libcamera::Stream* video_stream_ = nullptr; ///< The high-resolution video stream.
    libcamera::Stream* tpu_stream_ = nullptr; ///< The low-resolution TPU stream.
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_; ///< Allocates frame buffers.
    std::vector<std::unique_ptr<libcamera::Request>> requests_; ///< Vector of camera requests.
    
    libcamera::PixelFormat actual_pixel_format_; ///< The actual pixel format configured by libcamera.
    libcamera::Size actual_size_; ///< The actual resolution configured by libcamera.
    unsigned int actual_stride_ = 0; ///< The actual stride of the frame buffer.
    
    // Frame accounting counters
    std::atomic<int64_t> frames_produced_{0};
    std::atomic<int64_t> frames_consumed_by_inference_{0};
    
    // Application reference for updating counters
    class Application* app_ref_ = nullptr;

    std::chrono::steady_clock::time_point last_frame_time_; ///< Timestamp of the last processed frame.
    int frame_count_ = 0; ///< Counter for the number of processed frames.
    
    // FPS measurement variables
    std::chrono::steady_clock::time_point first_frame_time_; ///< Timestamp of the first frame for FPS calculation
    int fps_measurement_frames_ = 0; ///< Counter for FPS measurement frames

    std::unique_ptr<cv::VideoWriter> video_writer_;  ///< H.264 encoder (currently unused).
    std::function<void(cv::Mat& frame)> overlay_callback_;  ///< Callback for overlays.

    // Members for dedicated request processing thread
    std::thread request_processor_thread_;
    std::queue<libcamera::Request*> request_queue_;
    std::mutex request_queue_mutex_;
    std::condition_variable request_queue_cond_var_;

    int skip_initial_measurements_ = 20; // Number of initial frames to skip for performance metrics

    // Using MappedBufferInfo (globally defined) directly
    std::map<const libcamera::FrameBuffer*, MappedBufferInfo> mapped_buffers_;

    // Declaration without the 'mapped_buffers' parameter
    bool process_frame_buffer(const libcamera::FrameBuffer* fb,
                                           const libcamera::StreamConfiguration& cfg,
                                           std::shared_ptr<BufferPool<uint8_t>> buffer_pool,
                                           std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                                           ImageQueue& queue,
                                           const char* stream_name,
                                           unsigned int target_width,
                                           unsigned int target_height,
                                           std::chrono::steady_clock::time_point capture_time,
                                           uint64_t t_capture_raw_ms,
                                           const libcamera::PixelFormat& actual_format,
                                           long long frame_id,
                                           long long exposure_ms,
                                           std::atomic<int64_t>* main_stream_drop_counter,
                                           std::atomic<int64_t>* tpu_stream_drop_counter,
                                           std::atomic<int64_t>* frames_produced_counter);
    // New helper to process processed TPU frames
    bool process_tpu_processed_frame_buffer(const libcamera::FrameBuffer* fb,
                                           const libcamera::StreamConfiguration& cfg,
                                           std::chrono::steady_clock::time_point capture_time,
                                           uint64_t t_capture_raw_ms,
                                           long long frame_id,
                                           long long exposure_ms,
                                           uint64_t sensor_ts_ns);

    // Helper to process frames for direct inference when TPU is disabled
    bool process_frame_for_direct_inference(const libcamera::FrameBuffer* fb,
                                           const libcamera::StreamConfiguration& cfg,
                                           std::chrono::steady_clock::time_point capture_time,
                                           uint64_t t_capture_raw_ms,
                                           long long frame_id,
                                           long long exposure_ms,
                                           uint64_t sensor_ts_ns);

    void request_processor_thread_func(); // Added back missing declaration
    
    std::vector<ImageQueue*> main_output_queues_;  ///< Queues for BGR frames for the live stream.
    ImageQueue* direct_inference_queue_ = nullptr;  ///< Direct queue for inference when TPU is disabled.
}; // Corrected: closing brace for class CameraCapture

#endif // CAMERA_CAPTURE_H