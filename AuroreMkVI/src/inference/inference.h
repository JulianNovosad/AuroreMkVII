// OpenCV GPU-based inference - TFLite removed
// Verification timestamp: 2026-02-08
#ifndef INFERENCE_H
#define INFERENCE_H

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <queue>

#include "pipeline_structs.h"
#include "buffer_pool.h"
#include "timing.h"
#include "gpu_detector.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

class InferenceEngine {
public:
    InferenceEngine(const std::string& model_path,
                    ImageQueue& input_queue,
                    TripleBuffer<DetectionResults>* detection_results_for_overlay_buffer,
                    DetectionResultsQueue& detection_results_for_logic_queue,
                    std::shared_ptr<BufferPool<DetectionResult>> detection_result_pool,
                    std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                    std::shared_ptr<ObjectPool<ResultToken>> result_token_pool,
                    float score_threshold,
                    DetectionOverlayQueue* overlay_queue,
                    bool enable_tpu_inference,
                    bool enable_gpu_inference,
                    int num_threads = 1);

    ~InferenceEngine();

    bool start();
    void stop();
    // Public accessors to private members
    bool is_running() const { return running_.load(); } // Correctly calls .load()
    int get_input_width() const { return input_width_; }
    int get_input_height() const { return input_height_; }
    void get_state() const;
    
    long long get_inference_timing_us() const { return avg_inference_time_us_.load(); }

    // TPU monitoring metrics
    size_t get_input_queue_size() const { return input_queue_.size_approx(); }
    int get_current_inference_rate() const { return inference_rate_.load(); }  // inferences per second
    uint64_t get_last_inference_timestamp_ns() const { return last_inference_timestamp_ns_.load(); }
    
    // Queue monitoring for anomaly detection
    bool is_input_queue_stalled() const { 
        return get_input_queue_size() > 50 && inference_rate_.load() < 5; 
    }

    int64_t get_overlay_queue_drop_count() const { 
        if (detection_results_for_overlay_buffer_) {
            return detection_results_for_overlay_buffer_->get_drop_count();
        }
        return overlay_queue_drop_count_.load(); // Fallback if triple buffer is not used
    }
    
    bool has_overlay_pending() const {
        if (detection_results_for_overlay_buffer_) {
            return detection_results_for_overlay_buffer_->has_pending();
        }
        return false;
    }
    
    int64_t get_frames_consumed() const { return frames_consumed_.load(); }
    
    void set_application_ref(class Application* app) { app_ref_ = app; }
    std::vector<std::thread>& get_worker_threads() { return worker_threads_; }

    // Publicly exposed config flags
    // The actual flags are private members, this is an accessor to a private member for the constructor.
    // bool enable_tpu_inference_ = true; // This was here previously, but should be private.

private:
    void worker_thread_func();

    std::shared_ptr<DetectionResultBuffer> perform_gpu_detection(const ImageData& input_image);
    std::shared_ptr<DetectionResultBuffer> perform_opencv_detection(const ImageData& input_image);
    bool check_concentric_pattern(const cv::Mat& gray, const cv::Rect& bbox, float center_x, float center_y);
    std::shared_ptr<DetectionResultBuffer> validate_and_enforce_single_detection(
        std::shared_ptr<DetectionResultBuffer> input_buffer,
        const ImageData& input_image);

    std::string model_path_;
    ImageQueue& input_queue_;
    TripleBuffer<DetectionResults>* detection_results_for_overlay_buffer_;
    DetectionResultsQueue& detection_results_for_logic_queue_;
    DetectionOverlayQueue* overlay_queue_;
    std::shared_ptr<BufferPool<DetectionResult>> detection_result_pool_;
    std::shared_ptr<ObjectPool<ImageData>> image_data_pool_;
    std::shared_ptr<ObjectPool<ResultToken>> result_token_pool_;
    int num_threads_;
    float score_threshold_;
    // Reordered these two to match constructor initialization and resolve -Wreorder
    bool enable_tpu_inference_; // No default initialization here, handled by constructor
    bool enable_gpu_inference_; // Handled by constructor
    bool is_tpu_inference_enabled_; // New member

    int input_width_ = 0;
    int input_height_ = 0;
    int input_channels_ = 0;

    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_ = false; // This is a private member, accessed by is_running()
    
    std::unique_ptr<aurore::inference::GPUDetector> gpu_detector_;
    bool use_gpu_detection_ = false; // Track if GPU detection is actually available

    // Private atomic counters/variables
    std::atomic<uint64_t> last_inference_timestamp_ns_{0};
    std::atomic<int> inference_rate_{0};
    
    mutable std::atomic<long long> avg_inference_time_us_{0};
    
    std::atomic<int64_t> overlay_queue_drop_count_{0};
    std::atomic<int64_t> frames_consumed_{0};
    
    class Application* app_ref_ = nullptr;
    
    std::atomic<int> total_inference_count_{0};
    std::atomic<int> last_inference_count_checkpoint_{0};
    std::atomic<uint64_t> last_rate_check_ms_{0};

    // Temporal smoothing for OpenCV detection
    struct SmoothingState {
        float prev_x = 0.5f;
        float prev_y = 0.5f;
        float prev_size = 0.1f;
        float alpha = 0.5f;  // Smoothing factor (higher = less smoothing, more responsive)
        bool has_prev = false;
        uint64_t last_frame_id = 0;
    };
    SmoothingState smoothing_state_;
    std::mutex smoothing_mutex_;
};

#endif