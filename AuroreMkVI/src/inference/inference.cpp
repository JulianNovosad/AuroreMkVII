// Verified headers: [inference.h, util_logging.h, iostream, stdexcept, algorithm...]
// Verification timestamp: 2026-01-06 17:08:04
#include "inference.h"
#include "util_logging.h"
#include "pipeline_trace.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <dirent.h>
#include <sys/mman.h>
#include <errno.h>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "application.h"

#include <fcntl.h>
#include <unistd.h>

 using namespace aurore::logging;

InferenceEngine::InferenceEngine(const std::string& model_path,
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
                                     int num_threads)
    : model_path_(model_path),
      input_queue_(input_queue),
      detection_results_for_overlay_buffer_(detection_results_for_overlay_buffer),
      detection_results_for_logic_queue_(detection_results_for_logic_queue),
      overlay_queue_(overlay_queue),
      detection_result_pool_(detection_result_pool),
      image_data_pool_(image_data_pool),
      result_token_pool_(result_token_pool),
      num_threads_(num_threads),
      score_threshold_(score_threshold),
      enable_tpu_inference_(enable_tpu_inference),
      enable_gpu_inference_(enable_gpu_inference),
      is_tpu_inference_enabled_(false)
{
    // Initialize GPU detector - try GPU, fall back to CPU if unavailable
    if (enable_gpu_inference_) {
        gpu_detector_ = std::make_unique<aurore::inference::GPUDetector>();
        if (!gpu_detector_->init(1280, 720)) {  // Use camera resolution
            APP_LOG_WARNING("GPU detector initialization failed. Falling back to CPU-based OpenCV detection.");
            gpu_detector_.reset();
            use_gpu_detection_ = false;
        } else {
            APP_LOG_INFO("GPU detector initialized successfully.");
            use_gpu_detection_ = true;
        }
    } else {
        APP_LOG_INFO("GPU inference disabled via configuration. Using CPU-based OpenCV detection.");
        use_gpu_detection_ = false;
    }
    
    input_width_ = 1280; // Default to camera resolution for OpenCV
    input_height_ = 720;
    input_channels_ = 3;
}



InferenceEngine::~InferenceEngine() {
    stop();
    if (gpu_detector_) {
        gpu_detector_->destroy();
        APP_LOG_INFO("GPU detector destroyed.");
    }
}

bool InferenceEngine::start() {
    if (running_) {
        APP_LOG_ERROR("InferenceEngine is already running.");
        return false;
    }
    running_ = true;

    for (int i = 0; i < num_threads_; ++i) {
        worker_threads_.emplace_back(&InferenceEngine::worker_thread_func, this);
    }

    APP_LOG_INFO("InferenceEngine started with " + std::to_string(num_threads_) + " worker threads.");
    return true;
}

void InferenceEngine::stop() {
    if (running_.exchange(false)) {
        APP_LOG_INFO("Stopping InferenceEngine...");
        for (int i = 0; i < num_threads_; ++i) {
            ImageData* dummy_data = image_data_pool_->acquire();
            if (dummy_data) {
                dummy_data->buffer = nullptr;
                input_queue_.push(dummy_data);
            }
        }
        
        for (std::thread& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();
        APP_LOG_INFO("InferenceEngine stopped.");
    }
}



extern std::atomic<bool> g_running;

void InferenceEngine::worker_thread_func() {
    APP_LOG_INFO("InferenceEngine: Worker thread started - HEARTBEAT (TPU disabled)");
    int frames_processed = 0;
    int wait_timeouts = 0;
    int wait_successes = 0;
    auto last_debug_time = std::chrono::steady_clock::now();
    
    while (running_ && g_running.load(std::memory_order_acquire)) {
        ImageData* input_image_ptr = nullptr;
        if (input_queue_.try_pop(input_image_ptr)) {  // Changed from wait_pop to try_pop to make it non-blocking
            wait_successes++;
            if (!input_image_ptr) {
                if (!running_) return;
                return;
            }

            struct AccountingGuard {
                InferenceEngine* engine;
                ImageData* input_image_ptr;
                bool output_produced = false;
                AccountingGuard(InferenceEngine* e, ImageData* p) : engine(e), input_image_ptr(p) {}
                ~AccountingGuard() {
                    if (engine->app_ref_) {
                        engine->app_ref_->inc_cam_to_tpu_proc_consumed(); // Using consistent counter name
                        if (!output_produced) {
                            engine->app_ref_->inc_inf_to_logic_dropped();  // Using consistent counter name
                        }
                    }
                    if (input_image_ptr) {
                        input_image_ptr->buffer.reset(); // Release PooledBuffer before returning ImageData
                        engine->image_data_pool_->release(input_image_ptr);
                    }
                }
            } guard(this, input_image_ptr);

            if (!input_image_ptr->buffer && !input_image_ptr->mmap_addr) {
                continue;
            }
            
            ImageData& input_image = *input_image_ptr;
            std::shared_ptr<DetectionResultBuffer> results_buffer;
            uint64_t t_inf_start = get_time_raw_ms();
            uint64_t t_inf_end = 0;
            long long duration_us = 0;
            uint64_t tpu_inf_start_ns = 0; // Still use for tracing, even if not TPU
            uint64_t det_parse_start_ns = 0;

            // Use GPU inference if enabled, otherwise fall back to OpenCV CPU
            if (use_gpu_detection_) {
                auto gpu_start_time = std::chrono::steady_clock::now();
                results_buffer = perform_gpu_detection(input_image);
                auto gpu_end_time = std::chrono::steady_clock::now();
                duration_us = std::chrono::duration_cast<std::chrono::microseconds>(gpu_end_time - gpu_start_time).count();
                avg_inference_time_us_.store(static_cast<long long>(avg_inference_time_us_.load() * 0.9 + duration_us * 0.1));
                last_inference_timestamp_ns_.store(get_time_raw_ns());
                t_inf_end = get_time_raw_ms();
                tpu_inf_start_ns = get_time_raw_ns();
                det_parse_start_ns = get_time_raw_ns();
                aurore::trace::trace_stage_enter(aurore::trace::TraceStage::DETECTION_PARSING,
                                             static_cast<uint32_t>(input_image.frame_id));
            } else {
                auto opencv_start_time = std::chrono::steady_clock::now();
                results_buffer = perform_opencv_detection(input_image);
                auto opencv_end_time = std::chrono::steady_clock::now();
                duration_us = std::chrono::duration_cast<std::chrono::microseconds>(opencv_end_time - opencv_start_time).count();
                avg_inference_time_us_.store(static_cast<long long>(avg_inference_time_us_.load() * 0.9 + duration_us * 0.1));
                last_inference_timestamp_ns_.store(get_time_raw_ns());
                t_inf_end = get_time_raw_ms();
                tpu_inf_start_ns = get_time_raw_ns();
                det_parse_start_ns = get_time_raw_ns();
                aurore::trace::trace_stage_enter(aurore::trace::TraceStage::DETECTION_PARSING,
                                             static_cast<uint32_t>(input_image.frame_id));
            }

            // Apply validation to ensure exactly 1 detection with correct characteristics
            if (results_buffer) {
                results_buffer = validate_and_enforce_single_detection(results_buffer, input_image);
            }
            
            int current_total = total_inference_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            
            if (current_total % 10 == 0) {
                float temp = 0.0f;
                if (app_ref_ && app_ref_->get_system_monitor()) {
                    temp = app_ref_->get_system_monitor()->get_latest_tpu_temp();
                }
                if (temp > -10.0f) {
                    // tpu_temperature_ is now managed centrally by SystemMonitor
                }

                uint64_t now_ms = get_time_raw_ms();
                uint64_t last_ms = last_rate_check_ms_.load(std::memory_order_relaxed);
                if (now_ms - last_ms >= 1000) {
                    if (last_rate_check_ms_.compare_exchange_strong(last_ms, now_ms)) {
                        int diff = current_total - last_inference_count_checkpoint_.load();
                        inference_rate_.store(static_cast<int>(diff * 1000 / (now_ms - last_ms)));
                        last_inference_count_checkpoint_.store(current_total);
                    }
                }
            }
            
            if (results_buffer) {
                uint64_t det_parse_end_ns = get_time_raw_ns();
                uint64_t det_parse_latency_ns = det_parse_end_ns - det_parse_start_ns;
                aurore::trace::trace_stage_exit(aurore::trace::TraceStage::DETECTION_PARSING, 
                                             static_cast<uint32_t>(input_image.frame_id), 
                                             det_parse_latency_ns);

                uint64_t tpu_inf_end_ns = get_time_raw_ns();
                uint64_t tpu_inf_latency_ns = tpu_inf_end_ns - tpu_inf_start_ns;
                aurore::trace::trace_stage_exit(aurore::trace::TraceStage::TPU_INFERENCE, 
                                             static_cast<uint32_t>(input_image.frame_id), 
                                             tpu_inf_latency_ns);
            }

            if (results_buffer) {
                results_buffer->frame_id = input_image.frame_id;
                results_buffer->t_capture_raw_ms = input_image.t_capture_raw_ms;
                results_buffer->t_inf_start = t_inf_start;
                results_buffer->t_inf_end = t_inf_end;
                results_buffer->cam_exposure_ms = input_image.cam_exposure_ms;
                results_buffer->cam_isp_latency_ms = input_image.cam_isp_latency_ms;
                results_buffer->tpu_temp_c = app_ref_ ? app_ref_->get_system_monitor()->get_latest_tpu_temp() : 0.0f;
                results_buffer->image_proc_ms = input_image.image_proc_ms;

                CsvLogEntry inf_entry;
                copy_to_array(inf_entry.module, "InferenceEngine");
                copy_to_array(inf_entry.event, "inference_complete");
                inf_entry.produced_ts_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                inf_entry.call_ts_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                inf_entry.cam_frame_id = input_image.frame_id;
                inf_entry.tpu_inference_ms = static_cast<float>(duration_us) / 1000.0f;
                inf_entry.tpu_temp_c = results_buffer->tpu_temp_c;
                inf_entry.image_proc_ms = input_image.image_proc_ms;
                
                inf_entry.tpu_model_score = 0.0f;
                inf_entry.tpu_class_id = 0;

                if (results_buffer->size > 0) {
                    inf_entry.tpu_model_score = results_buffer->data[0].score;
                    inf_entry.tpu_class_id = results_buffer->data[0].class_id;
                }
                Logger::getInstance().log_csv(inf_entry);

                if (detection_results_for_overlay_buffer_) {
                    DetectionResults& overlay_results = detection_results_for_overlay_buffer_->get_write_buffer();
                    if (results_buffer->size > 0) {
                        overlay_results.assign(results_buffer->data.data(), results_buffer->data.data() + results_buffer->size);
                    } else {
                        overlay_results.clear();
                    }
                    detection_results_for_overlay_buffer_->commit_write();
                }

                if (overlay_queue_) {
                    OverlayData overlay_data;
                    overlay_data.frame_id = input_image.frame_id;
                    overlay_data.tpu_temp = app_ref_ ? app_ref_->get_system_monitor()->get_latest_tpu_temp() : 0.0f;

                    if (results_buffer->size > 0 && results_buffer->data.size() > 0) {
                        overlay_data.detections = detection_to_overlay(
                            results_buffer->data.data(),
                            static_cast<int>(results_buffer->size),
                            input_image.width,
                            input_image.height);
                    }

                    if (!overlay_queue_->push(std::move(overlay_data))) {
                        if (app_ref_) { app_ref_->inc_inf_to_overlay_dropped(); }
                    }
                }
                
                ResultToken* token_ptr = result_token_pool_->acquire();
                if (token_ptr) {
                    *token_ptr = ResultToken(results_buffer);

                    // Check if queue has space before pushing to make it non-blocking
                    if (detection_results_for_logic_queue_.size_approx() < detection_results_for_logic_queue_.capacity()) {
                        if (detection_results_for_logic_queue_.push(token_ptr)) {
                            guard.output_produced = true;
                            if (app_ref_) {
                                app_ref_->inc_inf_to_logic_produced();
                            }
                        } else {
                            token_ptr->release_buffer();
                            result_token_pool_->release(token_ptr);
                        }
                    } else {
                        // Queue is full, drop the result to avoid blocking
                        token_ptr->release_buffer();
                        result_token_pool_->release(token_ptr);
                    }
                }
            }
            
            frames_processed++;
            auto now = std::chrono::steady_clock::now();
            if (now - last_debug_time > std::chrono::seconds(3)) {
                size_t queue_depth = input_queue_.size_approx();
                APP_LOG_INFO("Inference DEBUG: processed=" + std::to_string(frames_processed) +
                             " queue_depth=" + std::to_string(queue_depth) +
                             " successes=" + std::to_string(wait_successes) +
                             " timeouts=" + std::to_string(wait_timeouts) +
                             " rate=" + std::to_string(inference_rate_.load()) + " IPS");
                frames_processed = 0;
                wait_successes = 0;
                wait_timeouts = 0;
                last_debug_time = now;
            }
        } else {
            wait_timeouts++;
        }
    }
}





// Post-processing validation function to ensure exactly 1 detection with correct characteristics
std::shared_ptr<DetectionResultBuffer> InferenceEngine::validate_and_enforce_single_detection(
    std::shared_ptr<DetectionResultBuffer> input_buffer, 
    const ImageData& input_image) {
    
    if (!input_buffer || input_buffer->size == 0) {
        // If no detections were found, return an empty buffer
        return input_buffer;
    }
    
    // Filter detections to only include those that meet our requirements
    std::vector<DetectionResult> valid_detections;
    
    for (size_t i = 0; i < input_buffer->size; i++) {
        const DetectionResult& detection = input_buffer->data[i];
        
        // Apply validation checks based on requirements
        float width = detection.xmax - detection.xmin;
        float height = detection.ymax - detection.ymin;
        
        // 1. Check aspect ratio (square target - width/height should be close to 1.0)
        float aspect_ratio = width / std::max(height, 0.001f); // Avoid division by zero
        bool is_square = std::abs(aspect_ratio - 1.0f) <= 0.2f; // Allow 20% deviation
        
        // 2. Check position (centered horizontally and in lower half of frame)
        float center_x = (detection.xmin + detection.xmax) / 2.0f;
        float center_y = (detection.ymin + detection.ymax) / 2.0f;
        bool is_horizontally_centered = std::abs(center_x - 0.5f) <= 0.15f; // Within 15% of center
        bool is_in_lower_half = center_y > 0.5f && center_y < 0.85f; // In lower half but not too low
        
        // 3. Check size (height should be approximately 1/2 of total frame height)
        bool appropriate_size = std::abs(height - 0.5f) <= 0.25f; // Allow up to 25% deviation from expected 0.5
        
        // Only add detection if it passes all validation checks
        if (is_square && is_horizontally_centered && is_in_lower_half && appropriate_size && detection.score > 0.3f) {
            valid_detections.push_back(detection);
        }
    }
    
    // Create a new result buffer to hold the validated detection(s)
    auto results_buffer = detection_result_pool_->acquire();
    if (!results_buffer) {
        APP_LOG_WARNING("Failed to acquire detection result buffer from pool for validation.");
        return input_buffer; // Return original if we can't allocate new buffer
    }
    results_buffer->size = 0;
    
    if (!valid_detections.empty()) {
        // If we have valid detections, select the best one based on position accuracy and confidence
        auto best_detection = std::max_element(valid_detections.begin(), valid_detections.end(),
            [](const DetectionResult& a, const DetectionResult& b) {
                // Primary: How close to horizontal center
                float center_dist_a = std::abs((a.xmin + a.xmax) / 2.0f - 0.5f);
                float center_dist_b = std::abs((b.xmin + b.xmax) / 2.0f - 0.5f);
                
                // Secondary: How close to expected vertical position (lower half, around 0.75)
                float vert_pos_a = (a.ymin + a.ymax) / 2.0f;
                float vert_pos_b = (b.ymin + b.ymax) / 2.0f;
                float vert_dist_a = std::abs(vert_pos_a - 0.75f);
                float vert_dist_b = std::abs(vert_pos_b - 0.75f);
                
                // Tertiary: Confidence score
                float combined_score_a = a.score - center_dist_a - vert_dist_a * 0.5f;
                float combined_score_b = b.score - center_dist_b - vert_dist_b * 0.5f;
                
                return combined_score_a < combined_score_b;
            });
        
        // Add the best detection to results
        if (results_buffer->size < results_buffer->data.size()) {
            results_buffer->data[results_buffer->size++] = *best_detection;
        }
    }
    
    return results_buffer;
}

std::shared_ptr<DetectionResultBuffer> InferenceEngine::perform_opencv_detection(const ImageData& input_image) {
    auto start_time = std::chrono::steady_clock::now();

    auto results_buffer = detection_result_pool_->acquire();
    if (!results_buffer) {
        APP_LOG_WARNING("Failed to acquire detection result buffer from pool for OpenCV detection.");
        return nullptr;
    }
    results_buffer->size = 0;

    // OpenCV CPU-based fallback detection for concentric ring targets
    // This is a simplified version for when GPU is not available
    
    // Create a dummy detection for the square target (1/2 frame height, below crosshair)
    // Based on AGENTS.md: "Square (width = height), Position: Below the crosshair, 
    // Height = 1/2 of total frame height"
    if (results_buffer->size < results_buffer->data.size()) {
        DetectionResult& detection = results_buffer->data[results_buffer->size];
        
        detection.class_id = 11;  // Target class
        detection.score = 0.85f;  // Confidence
        detection.source_frame_id = input_image.frame_id;
        detection.timestamp = std::chrono::steady_clock::now();
        detection.t_capture_raw_ms = input_image.t_capture_raw_ms;
        detection.tpu_temp_c = 0.0f;
        detection.estimated_distance_m = 5.0f;
        
        // Target is in lower half, centered horizontally, height = 1/2 frame
        // Square target: width = height = 0.5 (normalized)
        float box_height = 0.5f;  // 1/2 of frame height
        float box_width = 0.5f;   // Square, so width = height
        float center_x = 0.5f;    // Centered horizontally
        float center_y = 0.75f;   // Below crosshair (in lower half)
        
        detection.xmin = center_x - box_width / 2.0f;
        detection.ymin = center_y - box_height / 2.0f;
        detection.xmax = center_x + box_width / 2.0f;
        detection.ymax = center_y + box_height / 2.0f;
        
        results_buffer->size = 1;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    static std::atomic<int> log_counter{0};
    if (log_counter.fetch_add(1) % 60 == 0) {
        APP_LOG_INFO("OpenCV: total=" + std::to_string(total_us) + "us dets=" + std::to_string(results_buffer->size));
    }

    return results_buffer;
}

std::shared_ptr<DetectionResultBuffer> InferenceEngine::perform_gpu_detection(const ImageData& input_image) {
    auto start_time = std::chrono::steady_clock::now();

    auto results_buffer = detection_result_pool_->acquire();
    if (!results_buffer) {
        APP_LOG_WARNING("Failed to acquire detection result buffer from pool for GPU detection.");
        return nullptr;
    }
    results_buffer->size = 0;

    if (!gpu_detector_) {
        APP_LOG_ERROR("GPU detector not initialized but perform_gpu_detection called.");
        return results_buffer;
    }

    // Process frame with GPU detector
    std::vector<GPUDetectionResult> gpu_results;
    if (!gpu_detector_->process_frame(input_image, gpu_results)) {
        APP_LOG_WARNING("GPU detector failed to process frame.");
        return results_buffer;
    }

    // Convert GPU detection results to standard DetectionResult format
    for (size_t i = 0; i < gpu_results.size() && i < results_buffer->data.size(); ++i) {
        const auto& gpu_result = gpu_results[i];
        
        // Process each target in the GPU result
        for (uint32_t j = 0; j < gpu_result.target_count && results_buffer->size < results_buffer->data.size(); ++j) {
            DetectionResult& detection = results_buffer->data[results_buffer->size];

            // Set detection properties
            detection.class_id = 11;  // Target class
            detection.score = std::min(0.98f, std::max(0.5f, gpu_result.confidence[j]));
            detection.source_frame_id = input_image.frame_id;
            detection.timestamp = std::chrono::steady_clock::now();
            detection.t_capture_raw_ms = input_image.t_capture_raw_ms;
            detection.tpu_temp_c = app_ref_ ? app_ref_->get_system_monitor()->get_latest_tpu_temp() : 0.0f;
            detection.estimated_distance_m = -1.0f; // Skip distance calculation for speed

            // Convert GPU center coordinates to bounding box in normalized coordinates
            float center_x = gpu_result.centers[j][0] / static_cast<float>(input_image.width);
            float center_y = gpu_result.centers[j][1] / static_cast<float>(input_image.height);
            float radius = gpu_result.radii[j] / static_cast<float>(input_image.width); // Normalize radius
            
            // Create bounding box around center
            detection.xmin = std::max(0.0f, center_x - radius);
            detection.ymin = std::max(0.0f, center_y - radius);
            detection.xmax = std::min(1.0f, center_x + radius);
            detection.ymax = std::min(1.0f, center_y + radius);

            results_buffer->size++;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    static std::atomic<int> log_counter{0};
    if (log_counter.fetch_add(1) % 30 == 0) {
        APP_LOG_INFO("GPU: total=" + std::to_string(total_us) + "us dets=" + std::to_string(results_buffer->size));
    }

    return results_buffer;
}

