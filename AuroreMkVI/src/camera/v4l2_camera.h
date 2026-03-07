#ifndef V4L2_CAMERA_H
#define V4L2_CAMERA_H

#include <linux/videodev2.h>
#include <linux/media.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>

#include <opencv2/opencv.hpp>

#include "pipeline_structs.h"
#include "buffer_pool.h"
#include "atomic_ordering.h"
#include "priority_mutex.h"

struct V4L2Buffer {
    void* start;
    size_t length;
    int dmabuf_fd;  // DMABUF file descriptor for zero-copy
    uint64_t timestamp;
};

struct V4L2CameraConfig {
    unsigned int width;
    unsigned int height;
    unsigned int fps;
    unsigned int pixel_format;  // V4L2_PIX_FMT_RGB24, etc.
    unsigned int buffer_count;
};

/**
 * @class V4L2CameraCapture
 * @brief Direct V4L2 camera access for high FPS capture.
 * 
 * Bypasses libcamera PiSP handler to achieve 120 FPS by directly
 * selecting IMX708 sensor Mode 3 (1280×720 @ 120 FPS).
 */
class V4L2CameraCapture {
public:
    /**
     * @brief Constructor.
     * @param config Camera configuration (width, height, fps, format)
     * @param image_buffer_pool Pool for image buffers
     * @param image_data_pool Pool for ImageData objects
     * @param image_processor_input_queue Queue for processed frames
     * @param watchdog_timeout Watchdog timeout
     */
    V4L2CameraCapture(const V4L2CameraConfig& config,
                       std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool,
                       std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                       ImageQueue& image_processor_input_queue,
                       std::chrono::seconds watchdog_timeout);
    
    ~V4L2CameraCapture();

    /**
     * @brief Start video capture.
     * @return True if successful.
     */
    bool start();

    /**
     * @brief Stop video capture.
     */
    void stop();

    /**
     * @brief Check if capture is running.
     */
    bool is_running() const { return running_; }

    /**
     * @brief Get camera state.
     */
    void get_state() const;

    /**
     * @brief Set overlay callback.
     */
    void set_overlay_callback(std::function<void(cv::Mat& frame)> callback) {
        overlay_callback_ = callback;
    }

    // Timing methods
    long long get_capture_timing_us() const { return Aurore::atomic_load_acquire(avg_capture_time_us_); }
    long long get_total_loop_timing_us() const { return Aurore::atomic_load_acquire(avg_total_loop_time_us_); }

    // Freshness indicators
    std::atomic<long long> last_frame_timestamp_{0};
    std::atomic<int> frame_rate_{0};

    // Frame accounting
    std::atomic<int64_t> frames_produced_{0};
    std::atomic<int64_t> frames_consumed_by_inference_{0};

    // Drop counters
    std::atomic<int64_t> tpu_stream_drop_count_{0};
    int64_t get_tpu_stream_drop_count() const { return Aurore::atomic_load_acquire(tpu_stream_drop_count_); }

    // Application reference
    void set_application_ref(class Application* app) { app_ref_ = app; }

private:
    // V4L2 device operations
    bool open_device();
    void close_device();
    bool query_device();
    bool set_sensor_mode(unsigned int mode);
    bool set_format();
    bool set_params();
    bool request_buffers();
    bool queue_buffers();
    bool start_capture();
    void stop_capture();
    
    // Capture loop
    void capture_thread_func();
    
    // Buffer processing
    bool process_buffer(int index);
    bool export_dmabuf(int index, int* out_fd);
    
    // Configuration
    V4L2CameraConfig config_;
    std::string device_path_;
    int fd_;
    
    // Buffers
    std::vector<V4L2Buffer> buffers_;
    std::vector<int> queued_buffers_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cond_;
    
    // Thread control
    std::atomic<bool> running_{false};
    std::thread capture_thread_;
    
    // Timing
    mutable std::atomic<long long> avg_capture_time_us_{0};
    mutable std::atomic<long long> avg_total_loop_time_us_{0};
    
    // Pools and queues
    std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool_;
    std::shared_ptr<ObjectPool<ImageData>> image_data_pool_;
    ImageQueue& image_processor_input_queue_;
    std::chrono::seconds watchdog_timeout_;
    
    // Overlay callback
    std::function<void(cv::Mat& frame)> overlay_callback_;
    
    // Application reference
    class Application* app_ref_ = nullptr;
    
    // Frame timing
    std::chrono::steady_clock::time_point first_frame_time_;
    int frame_count_;
    long long frame_id_;
    
    // Watchdog
    std::chrono::steady_clock::time_point last_activity_;
    std::thread watchdog_thread_;
    std::atomic<bool> watchdog_running_{false};
    void watchdog_thread_func();
};

// IMX708 Sensor Mode definitions
#define IMX708_MODE_0 0  // 4608x2592 @ 14 FPS
#define IMX708_MODE_1 1  // 2304x1296 @ 30 FPS
#define IMX708_MODE_2 2  // 1536x864 @ 60 FPS
#define IMX708_MODE_3 3  // 1280x720 @ 120 FPS ← TARGET

#endif // V4L2_CAMERA_H
