#include "gpu_detector.h"
#include "util_logging.h"
#include <algorithm>
#include <cmath>

namespace aurore {
namespace inference {

GPUDetector::GPUDetector()
    : frame_width_(1280)
    , frame_height_(720)
    , initialized_(false)
    , available_(false)
    , enabled_(false)
    , detections_per_sec_(0)
    , processing_time_us_(0.0f)
    , failed_frames_(0)
    , software_mode_(false)
    , gl_ctx_(aurore::gpu::GLContext::get_instance())
    , texture_pool_()
    , vao_(0)
    , vbo_(0)
    , camera_texture_(0)
    , intermediate_texture_(0)
    , binary_texture_(0)
    , filtered_texture_(0)
    , morph_texture_(0)
    , edge_texture_(0)
    , accum_texture_(0)
    , centroid_texture_(0)
    , result_texture_(0)
    , framebuffer_(0)
    , program_(0)
    , u_camera_loc_(-1)
    , u_frame_size_loc_(-1)
    , u_center_x_loc_(-1)
    , u_center_y_loc_(-1)
    , u_max_radius_loc_(-1)
    , frame_count_(0)
    , detection_count_(0)
{
    last_fps_update_ = std::chrono::steady_clock::now();
}

GPUDetector::~GPUDetector() {
    destroy();
}

bool GPUDetector::init(int frame_width, int frame_height) {
    if (initialized_) {
        return true;
    }

    frame_width_ = frame_width;
    frame_height_ = frame_height;

    if (!gl_ctx_.init()) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize GL context");
        return false;
    }

    software_mode_ = gl_ctx_.is_software_mode();
    APP_LOG_INFO("GPUDetector: Software mode: " + std::string(software_mode_ ? "YES" : "NO"));

    if (software_mode_) {
        available_ = true;
        enabled_ = true;
        initialized_ = true;
        APP_LOG_INFO("GPUDetector: Using software ring detection");
        return true;
    }

    if (!texture_pool_.init(4)) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize texture pool");
        return false;
    }

    if (!init_compute_pipelines(frame_width, frame_height)) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize compute pipelines");
        return false;
    }

    pixel_buffer_.resize(frame_width * frame_height * 4);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    APP_LOG_INFO("GPUDetector: Initialized successfully");
    initialized_ = true;
    available_ = true;
    enabled_ = true;

    return true;
}

void GPUDetector::destroy() {
    if (!initialized_) {
        return;
    }

    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (camera_texture_) glDeleteTextures(1, &camera_texture_);
    if (result_texture_) glDeleteTextures(1, &result_texture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    if (program_) glDeleteProgram(program_);

    texture_pool_.destroy();

    initialized_ = false;
    available_ = false;
}

bool GPUDetector::init_compute_pipelines(int width, int height) {
    if (!texture_pool_.init(4)) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize texture pool");
        return false;
    }

    glGenTextures(1, &camera_texture_);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &result_texture_);
    glBindTexture(GL_TEXTURE_2D, result_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, result_texture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        APP_LOG_ERROR("GPUDetector: Framebuffer incomplete: 0x" + std::to_string(status));
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    APP_LOG_INFO("GPUDetector: Framebuffers initialized for " +
                 std::to_string(width) + "x" + std::to_string(height));
    return true;
}


bool GPUDetector::process_frame(const ImageData& image_data,
                                 std::vector<GPUDetectionResult>& results) {
    if (!enabled_.load(std::memory_order_acquire) || !initialized_) {
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();

    if (software_mode_) {
        return process_frame_software(image_data, results);
    }

    if (!upload_frame(image_data)) {
        failed_frames_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    run_8pass_pipeline();
    read_detection_results(results);

    auto end_time = std::chrono::steady_clock::now();
    float elapsed_us = std::chrono::duration<float, std::micro>(end_time - start_time).count();
    processing_time_us_.store(elapsed_us, std::memory_order_relaxed);

    frame_count_++;
    detection_count_ += results.size();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_fps_update_).count();
    if (elapsed >= 1) {
        detections_per_sec_.store(detection_count_ / elapsed, std::memory_order_relaxed);
        detection_count_ = 0;
        last_fps_update_ = now;
    }

    if (detection_callback_ && !results.empty()) {
        detection_callback_(results);
    }

    return true;
}

bool GPUDetector::upload_frame(const ImageData& image_data) {
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_width_, frame_height_,
                    GL_RED, GL_UNSIGNED_BYTE, image_data.buffer->data.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void GPUDetector::run_8pass_pipeline() {
    // Run the detection pipeline by iterating through the detection grid
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glUniform1i(u_camera_loc_, 0);
    glUniform2f(u_frame_size_loc_, static_cast<float>(frame_width_),
                static_cast<float>(frame_height_));

    int candidate_spacing = 80;
    int grid_width = frame_width_ / candidate_spacing;
    int grid_height = frame_height_ / candidate_spacing;

    float max_radius = static_cast<float>(std::min(frame_width_, frame_height_)) / 2.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, frame_width_, frame_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(vao_);

    for (int row = 0; row < grid_height; row++) {
        for (int col = 0; col < grid_width; col++) {
            float cx = (col + 0.5f) * candidate_spacing;
            float cy = (row + 0.5f) * candidate_spacing;

            glUniform1f(u_center_x_loc_, cx);
            glUniform1f(u_center_y_loc_, cy);
            glUniform1f(u_max_radius_loc_, max_radius);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
}

void GPUDetector::read_detection_results(std::vector<GPUDetectionResult>& results) {
    GPUDetectionResult result = {};
    result.frame_id = frame_count_;
    result.target_count = 0;
    for (int i = 0; i < 10; i++) {
        result.radii[i] = 0.0f;
        result.confidence[i] = 0.0f;
        result.centers[i][0] = 0.0f;
        result.centers[i][1] = 0.0f;
    }

    glBindTexture(GL_TEXTURE_2D, result_texture_);
    glReadPixels(0, 0, frame_width_, frame_height_, GL_RGBA, GL_FLOAT, pixel_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    int candidate_spacing = 80;
    int grid_width = frame_width_ / candidate_spacing;
    int grid_height = frame_height_ / candidate_spacing;

    // Collect all potential detections that match our target requirements
    std::vector<GPUDetectionResult> valid_detections;
    
    for (int row = 0; row < grid_height; row++) {
        for (int col = 0; col < grid_width; col++) {
            // Calculate the position in the grid where detection results are stored
            int center_x = (col + 0.5f) * candidate_spacing;
            int center_y = (row + 0.5f) * candidate_spacing;
            
            // Convert to pixel buffer index (each pixel has 4 floats for RGBA)
            int pixel_idx = (center_y * frame_width_ + center_x) * 4;
            if (pixel_idx + 3 >= pixel_buffer_.size()) continue;
            
            float norm_xmin = pixel_buffer_[pixel_idx];
            float norm_ymin = pixel_buffer_[pixel_idx + 1];
            float norm_xmax = pixel_buffer_[pixel_idx + 2];
            float rings_info = pixel_buffer_[pixel_idx + 3]; // This contains rings info for invalid detections

            // Valid detection has norm_xmin >= 0.0 (from shader), invalid has norm_xmin = -1.0
            if (norm_xmin >= 0.0f) {
                // Convert normalized coordinates back to pixel coordinates
                float xmin = norm_xmin * frame_width_;
                float ymin = norm_ymin * frame_height_;
                float xmax = norm_xmax * frame_width_;
                float ymax = ymin + (xmax - xmin); // Assuming square target
                
                float center_x_px = (xmin + xmax) / 2.0f;
                float center_y_px = (ymin + ymax) / 2.0f;
                float width = xmax - xmin;
                float height = ymax - ymin;
                float radius = width / 2.0f;

                // Apply target constraints as per AGENTS.md:
                // 1. Target should be square-like (aspect ratio close to 1)
                // 2. Target should be in the lower half of the frame (below crosshair, upper part of lower half)
                // 3. Target should be approximately centered horizontally
                float aspect_ratio = width / height;
                bool is_square_like = abs(aspect_ratio - 1.0f) < 0.2f; // Tighter constraint: 20% deviation
                
                bool in_lower_half = center_y_px > (frame_height_ / 2.0f) && center_y_px < frame_height_;
                bool is_horizontally_centered = abs(center_x_px - (frame_width_ / 2.0f)) < (frame_width_ / 8.0f); // Tighter constraint: within middle eighth

                // Target should be of appropriate size (about 1/2 of frame height = 360px for 720p)
                bool appropriate_size = abs(height - (frame_height_ / 2.0f)) < (frame_height_ / 4.0f); // Within 25% tolerance

                if (is_square_like && in_lower_half && is_horizontally_centered && appropriate_size) {
                    GPUDetectionResult temp_result = {};
                    temp_result.frame_id = frame_count_;
                    temp_result.target_count = 1;
                    
                    temp_result.centers[0][0] = center_x_px;
                    temp_result.centers[0][1] = center_y_px;
                    temp_result.radii[0] = radius;
                    // Base confidence from rings, enhanced with positional confidence
                    float pos_confidence = (1.0f - abs(center_x_px - (frame_width_/2.0f))/(frame_width_/8.0f)) * // Horizontal centering
                                          (1.0f - abs(center_y_px - (frame_height_* 0.75f))/(frame_height_/4.0f)); // Lower half positioning
                    temp_result.confidence[0] = std::min(1.0f, std::max(0.3f, pos_confidence)); // Ensure minimum confidence
                    
                    valid_detections.push_back(temp_result);
                }
            }
        }
    }

    // Select exactly one detection based on our priority:
    // 1. If we have valid detections, pick the one with the best positional characteristics
    // 2. If no valid detections, return an empty result
    if (!valid_detections.empty()) {
        // Find the detection that best matches our target requirements
        auto best_it = std::max_element(valid_detections.begin(), valid_detections.end(),
            [this](const GPUDetectionResult& a, const GPUDetectionResult& b) {
                // Primary: How close to horizontal center
                float dist_a_x = std::abs(a.centers[0][0] - (frame_width_ / 2.0f));
                float dist_b_x = std::abs(b.centers[0][0] - (frame_width_ / 2.0f));
                
                // Secondary: How close to expected vertical position (lower half, around 3/4 down)
                float expected_y = frame_height_ * 0.75f;
                float dist_a_y = std::abs(a.centers[0][1] - expected_y);
                float dist_b_y = std::abs(b.centers[0][1] - expected_y);
                
                // Compare combined distance (prefer closer to center in both dimensions)
                float total_dist_a = dist_a_x + dist_a_y;
                float total_dist_b = dist_b_x + dist_b_y;
                
                return total_dist_a > total_dist_b; // Lower distance is better
            });

        // Copy the best detection to our result
        result = *best_it;
        result.target_count = 1;
    } else {
        // No valid detections found that match our criteria
        result.target_count = 0;
    }

    result.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    result.processing_time_ms = 0.0f;

    results.push_back(result);
}

bool GPUDetector::process_frame_software(const ImageData& image_data,
                                          std::vector<GPUDetectionResult>& results) {
    auto start_time = std::chrono::steady_clock::now();
    
    GPUDetectionResult result = {};
    result.frame_id = frame_count_;
    result.target_count = 0;
    for (int i = 0; i < 10; i++) {
        result.radii[i] = 0.0f;
        result.confidence[i] = 0.0f;
        result.centers[i][0] = 0.0f;
        result.centers[i][1] = 0.0f;
    }

    const uint8_t* data = image_data.buffer->data.data();
    int width = frame_width_;
    int height = frame_height_;
    int candidate_spacing = 80;
    int grid_width = width / candidate_spacing;
    int grid_height = height / candidate_spacing;
    float max_radius = std::min(width, height) / 2.0f;

    // Collect all potential detections that match our target requirements
    std::vector<GPUDetectionResult> valid_detections;

    for (int row = 0; row < grid_height; row++) {
        for (int col = 0; col < grid_width; col++) {
            float cx = (col + 0.5f) * candidate_spacing;
            float cy = (row + 0.5f) * candidate_spacing;

            int num_directions = 8;
            int samples_per_dir = 64;
            int total_transitions = 0;

            for (int d = 0; d < num_directions; d++) {
                float angle = d * 3.14159265f / 4.0f;
                float dx = cosf(angle);
                float dy = sinf(angle);

                int transitions = 0;
                float prev_intensity = -1.0f;

                for (int s = 0; s < samples_per_dir; s++) {
                    float t = (float)(s + 1) * max_radius / samples_per_dir;
                    int px = (int)(cx + dx * t);
                    int py = (int)(cy + dy * t);

                    if (px < 0 || px >= width || py < 0 || py >= height) {
                        break;
                    }

                    float intensity = data[py * width + px] / 255.0f;

                    if (prev_intensity >= 0.0f && fabs(intensity - prev_intensity) > 0.15f) {
                        transitions++;
                    }
                    prev_intensity = intensity;
                }

                int rings = transitions / 2;
                total_transitions += rings;
            }

            int avg_rings = total_transitions / num_directions;

            if (avg_rings >= 3) {
                // Apply target constraints as per AGENTS.md:
                // 1. Target should be square-like (aspect ratio close to 1)
                // 2. Target should be in the lower half of the frame (below crosshair, upper part of lower half)
                // 3. Target should be approximately centered horizontally
                bool in_lower_half = cy > (height / 2.0f) && cy < height;
                bool is_horizontally_centered = abs(cx - (width / 2.0f)) < (width / 8.0f); // Within middle eighth
                bool appropriate_size = abs(max_radius - (height / 4.0f)) < (height / 8.0f); // Around 1/4 of frame height (for diameter of 1/2 frame height)

                if (in_lower_half && is_horizontally_centered && appropriate_size) {
                    GPUDetectionResult temp_result = {};
                    temp_result.frame_id = frame_count_;
                    temp_result.target_count = 1;
                    
                    temp_result.centers[0][0] = cx;  // Store in pixel coordinates
                    temp_result.centers[0][1] = cy;
                    temp_result.radii[0] = max_radius;
                    // Enhance confidence based on how well it fits target requirements
                    float pos_confidence = (1.0f - abs(cx - (width/2.0f))/(width/8.0f)) * // Horizontal centering
                                          (1.0f - abs(cy - (height * 0.75f))/(height/4.0f)); // Lower half positioning
                    temp_result.confidence[0] = std::min(1.0f, std::max(0.3f, pos_confidence));
                    
                    valid_detections.push_back(temp_result);
                }
            }
        }
    }

    // Select exactly one detection based on our priority:
    if (!valid_detections.empty()) {
        // Find the detection that best matches our target requirements
        auto best_it = std::max_element(valid_detections.begin(), valid_detections.end(),
            [width, height](const GPUDetectionResult& a, const GPUDetectionResult& b) {
                // Primary: How close to horizontal center
                float dist_a_x = std::abs(a.centers[0][0] - (width / 2.0f));
                float dist_b_x = std::abs(b.centers[0][0] - (width / 2.0f));
                
                // Secondary: How close to expected vertical position (lower half, around 3/4 down)
                float expected_y = height * 0.75f;
                float dist_a_y = std::abs(a.centers[0][1] - expected_y);
                float dist_b_y = std::abs(b.centers[0][1] - expected_y);
                
                // Compare combined distance (prefer closer to center in both dimensions)
                float total_dist_a = dist_a_x + dist_a_y;
                float total_dist_b = dist_b_x + dist_b_y;
                
                return total_dist_a > total_dist_b; // Lower distance is better
            });

        // Copy the best detection to our result
        result = *best_it;
        result.target_count = 1;
    } else {
        // No valid detections found that match our criteria
        result.target_count = 0;
    }

    result.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    result.processing_time_ms = 0.0f;

    results.push_back(result);

    auto end_time = std::chrono::steady_clock::now();
    float elapsed_us = std::chrono::duration<float, std::micro>(end_time - start_time).count();
    processing_time_us_.store(elapsed_us, std::memory_order_relaxed);
    result.processing_time_ms = elapsed_us / 1000.0f;

    frame_count_++;
    detection_count_ += result.target_count;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_fps_update_).count();
    if (elapsed >= 1) {
        detections_per_sec_.store(detection_count_ / elapsed, std::memory_order_relaxed);
        detection_count_ = 0;
        last_fps_update_ = now;
    }

    if (detection_callback_ && result.target_count > 0) {
        detection_callback_(results);
    }

    return true;
}

} // namespace inference
} // namespace aurore
