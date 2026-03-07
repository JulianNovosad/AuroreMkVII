#include "gpu_detector.h"
#include "util_logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace aurore {
namespace inference {

static const char* VERTEX_SHADER = R"(
#version 310 es
layout(location = 0) in vec2 a_position;
out vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = (a_position + 1.0) * 0.5;
}
)";

static const char* RING_FRAGMENT_SHADER = R"(
#version 310 es
precision highp float;
uniform sampler2D u_camera;
uniform vec2 u_frame_size;
uniform vec2 u_center;
uniform float u_max_radius;
in vec2 v_texCoord;
out vec4 fragColor;
void main() {
    vec2 pos = v_texCoord * u_frame_size;
    vec2 diff = pos - u_center;
    float dist = length(diff);
    if (dist > u_max_radius || dist < 2.0) {
        discard;
    }
    float normalized = dist / u_max_radius;
    float intensity = texture(u_camera, v_texCoord).r;
    fragColor = vec4(normalized, intensity, float(int(dist) % 20), 1.0);
}
)";

static const char* THRESHOLD_FRAGMENT_SHADER = R"(
#version 310 es
precision highp float;
uniform sampler2D u_texture;
in vec2 v_texCoord;
out vec4 fragColor;
void main() {
    float intensity = texture(u_texture, v_texCoord).r;
    float threshold = 0.5;
    fragColor = vec4(0.0, 0.0, 0.0, step(threshold, intensity));
}
)";

GPUDetector::GPUDetector()
    : frame_width_(1280), frame_height_(720), initialized_(false),
      available_(false), enabled_(false), detections_per_sec_(0),
      processing_time_us_(0.0f), failed_frames_(0), software_mode_(false),
      gl_ctx_(aurore::gpu::GLContext::get_instance()),
      texture_pool_(), vao_(0), vbo_(0),
      camera_texture_(0), intermediate_texture_(0), binary_texture_(0),
      filtered_texture_(0), morph_texture_(0), edge_texture_(0),
      accum_texture_(0), centroid_texture_(0),
      programs_(), framebuffers_{},
      pass_times_(), current_pass_(0)
{
    last_fps_update_ = std::chrono::steady_clock::now();
    frame_count_ = 0;
    detection_count_ = 0;
}

GPUDetector::~GPUDetector() { destroy(); }

bool GPUDetector::init(int frame_width, int frame_height) {
    if (initialized_) return true;

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
        APP_LOG_INFO("GPUDetector: Using software ring detection (fallback)");
        return true;
    }

    if (!init_compute_pipelines(frame_width, frame_height)) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize compute pipelines");
        return false;
    }

    APP_LOG_INFO("GPUDetector: Compute pipelines initialized successfully for " +
                 std::to_string(frame_width) + "x" + std::to_string(frame_height));
    initialized_ = true;
    available_ = true;
    enabled_ = true;

    return true;
}

bool GPUDetector::init_compute_pipelines(int width, int height) {
    if (!texture_pool_.init(8)) {
        APP_LOG_ERROR("GPUDetector: Failed to initialize texture pool");
        return false;
    }

    glGenTextures(1, &camera_texture_);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint vert_shader = aurore::gpu::compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
    if (!vert_shader) {
        APP_LOG_ERROR("GPUDetector: Failed to compile vertex shader");
        return false;
    }

    GLuint ring_shader = aurore::gpu::compile_shader(GL_FRAGMENT_SHADER, RING_FRAGMENT_SHADER);
    if (!ring_shader) {
        APP_LOG_ERROR("GPUDetector: Failed to compile ring fragment shader");
        glDeleteShader(vert_shader);
        return false;
    }
    programs_[0] = aurore::gpu::link_program(vert_shader, ring_shader);
    if (!programs_[0]) {
        APP_LOG_ERROR("GPUDetector: Failed to link ring program");
        return false;
    }

    GLuint thresh_shader = aurore::gpu::compile_shader(GL_FRAGMENT_SHADER, THRESHOLD_FRAGMENT_SHADER);
    if (!thresh_shader) {
        APP_LOG_ERROR("GPUDetector: Failed to compile threshold shader");
        return false;
    }
    programs_[1] = aurore::gpu::link_program(vert_shader, thresh_shader);
    if (!programs_[1]) {
        APP_LOG_ERROR("GPUDetector: Failed to link threshold program");
        return false;
    }

    glDeleteShader(vert_shader);
    glDeleteShader(ring_shader);
    glDeleteShader(thresh_shader);

    glGenTextures(1, &intermediate_texture_);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &intermediate_texture_);
    glBindTexture(GL_TEXTURE_2D, intermediate_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &binary_texture_);
    glBindTexture(GL_TEXTURE_2D, binary_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &filtered_texture_);
    glBindTexture(GL_TEXTURE_2D, filtered_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &morph_texture_);
    glBindTexture(GL_TEXTURE_2D, morph_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &edge_texture_);
    glBindTexture(GL_TEXTURE_2D, edge_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    int accum_width = width / 2;
    int accum_height = height / 2;
    glGenTextures(1, &accum_texture_);
    glBindTexture(GL_TEXTURE_2D, accum_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, accum_width, accum_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &centroid_texture_);
    glBindTexture(GL_TEXTURE_2D, centroid_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 10, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (int i = 0; i < 6; i++) {
        glGenFramebuffers(1, &framebuffers_[i]);
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return true;
}

void GPUDetector::destroy() {
    if (!initialized_) return;

    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (camera_texture_) glDeleteTextures(1, &camera_texture_);
    if (intermediate_texture_) glDeleteTextures(1, &intermediate_texture_);
    if (binary_texture_) glDeleteTextures(1, &binary_texture_);
    if (filtered_texture_) glDeleteTextures(1, &filtered_texture_);
    if (morph_texture_) glDeleteTextures(1, &morph_texture_);
    if (edge_texture_) glDeleteTextures(1, &edge_texture_);
    if (accum_texture_) glDeleteTextures(1, &accum_texture_);
    if (centroid_texture_) glDeleteTextures(1, &centroid_texture_);

    for (int i = 0; i < 6; i++) {
        if (framebuffers_[i]) glDeleteFramebuffers(1, &framebuffers_[i]);
    }

    for (auto& prog : programs_) {
        if (prog) glDeleteProgram(prog);
    }

    texture_pool_.destroy();
    initialized_ = false;
    available_ = false;
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

    auto upload_start = std::chrono::steady_clock::now();
    if (!upload_frame(image_data)) {
        failed_frames_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    pass_times_[0] = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - upload_start).count();

    run_8pass_pipeline();
    read_detection_results(results);

    auto end_time = std::chrono::steady_clock::now();
    float elapsed_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    processing_time_us_.store(elapsed_ms * 1000.0f, std::memory_order_relaxed);

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
    current_pass_ = 0;

    auto pass_start = std::chrono::steady_clock::now();

    glUseProgram(programs_[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, camera_texture_);
    glUniform1i(glGetUniformLocation(programs_[0], "u_camera"), 0);

    int candidate_spacing = 80;
    int grid_width = frame_width_ / candidate_spacing;
    int grid_height = frame_height_ / candidate_spacing;
    float max_radius = static_cast<float>(std::min(frame_width_, frame_height_)) / 2.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[0]);
    glViewport(0, 0, frame_width_, frame_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform2f(glGetUniformLocation(programs_[0], "u_frame_size"),
                static_cast<float>(frame_width_), static_cast<float>(frame_height_));
    glUniform1f(glGetUniformLocation(programs_[0], "u_max_radius"), max_radius);

    glBindVertexArray(vao_);

    for (int row = 0; row < grid_height; row++) {
        for (int col = 0; col < grid_width; col++) {
            float cx = (col + 0.5f) * candidate_spacing;
            float cy = (row + 0.5f) * candidate_spacing;

            glUniform2f(glGetUniformLocation(programs_[0], "u_center"), cx, cy);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);

    pass_times_[1] = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - pass_start).count();
    pass_times_[2] = 0.0f;
    pass_times_[3] = 0.0f;
    pass_times_[4] = 0.0f;
    pass_times_[5] = 0.0f;
    pass_times_[6] = 0.0f;
    pass_times_[7] = 0.0f;
    pass_times_[8] = 0.0f;

    current_pass_ = 8;
}

void GPUDetector::read_detection_results(std::vector<GPUDetectionResult>& results) {
    GPUDetectionResult result;
    result.frame_id = 0;
    result.target_count = 0;
    result.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    result.processing_time_ms = 0.0f;
    for (int i = 0; i < 8; i++) {
        result.processing_time_ms += pass_times_[i + 1];
    }

    // Initialize all arrays to zero
    for (int i = 0; i < 10; i++) {
        result.radii[i] = 0.0f;
        result.confidence[i] = 0.0f;
        result.centers[i][0] = 0.0f;
        result.centers[i][1] = 0.0f;
    }

    glBindTexture(GL_TEXTURE_2D, centroid_texture_);
    std::vector<uint8_t> centroid_data(40);
    glReadPixels(0, 0, 10, 1, GL_RGBA, GL_UNSIGNED_BYTE, centroid_data.data());

    // Collect all potential detections that match our target requirements
    std::vector<GPUDetectionResult> valid_detections;

    for (int i = 0; i < 10; i++) {
        uint8_t* circle = &centroid_data[i * 4];
        if (circle[3] > 0) { // Valid detection
            float center_x = static_cast<float>(circle[0]);
            float center_y = static_cast<float>(circle[1]);
            float radius = static_cast<float>(circle[2]);
            float confidence = static_cast<float>(circle[3]) / 255.0f;

            // Apply target constraints as per AGENTS.md:
            // 1. Target should be square-like (aspect ratio close to 1)
            // 2. Target should be in the lower half of the frame (below crosshair, upper part of lower half)
            // 3. Target should be approximately centered horizontally
            bool in_lower_half = center_y > (frame_height_ / 2.0f) && center_y < frame_height_;
            bool is_horizontally_centered = abs(center_x - (frame_width_ / 2.0f)) < (frame_width_ / 8.0f); // Within middle eighth
            bool appropriate_size = abs(radius - (frame_height_ / 4.0f)) < (frame_height_ / 8.0f); // Around 1/4 of frame height (for diameter of 1/2 frame height)

            if (in_lower_half && is_horizontally_centered && appropriate_size) {
                GPUDetectionResult temp_result = {};
                temp_result.frame_id = frame_count_;
                temp_result.target_count = 1;
                
                temp_result.centers[0][0] = center_x;
                temp_result.centers[0][1] = center_y;
                temp_result.radii[0] = radius;
                // Enhance confidence based on how well it fits target requirements
                float pos_confidence = (1.0f - abs(center_x - (frame_width_/2.0f))/(frame_width_/8.0f)) * // Horizontal centering
                                      (1.0f - abs(center_y - (frame_height_ * 0.75f))/(frame_height_/4.0f)); // Lower half positioning
                temp_result.confidence[0] = std::min(1.0f, std::max(confidence, pos_confidence));

                valid_detections.push_back(temp_result);
            }
        }
    }

    // Select exactly one detection based on our priority:
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

    results.push_back(result);
}

bool GPUDetector::process_frame_software(const ImageData& image_data,
                                          std::vector<GPUDetectionResult>& results) {
    const uint8_t* data = image_data.buffer->data.data();
    int width = frame_width_;
    int height = frame_height_;

    GPUDetectionResult gpu_result;
    gpu_result.frame_id = image_data.frame_id;
    gpu_result.target_count = 0;
    gpu_result.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto start_time = std::chrono::steady_clock::now();

    int target_height = height / 2;
    int target_width = target_height;
    int target_center_x = width / 2;
    int target_center_y = height * 3 / 4;

    int roi_x1 = target_center_x - target_width / 2;
    int roi_y1 = target_center_y - target_height / 2;
    int roi_x2 = target_center_x + target_width / 2;
    int roi_y2 = target_center_y + target_height / 2;

    roi_x1 = std::max(0, roi_x1);
    roi_y1 = std::max(0, roi_y1);
    roi_x2 = std::min(width - 1, roi_x2);
    roi_y2 = std::min(height - 1, roi_y2);

    int roi_width = roi_x2 - roi_x1 + 1;
    int roi_height = roi_y2 - roi_y1 + 1;

    if (roi_width < 10 || roi_height < 10) {
        if (gpu_result.target_count > 0) {
            results.push_back(gpu_result);
        }
        return true;
    }

    std::vector<float> row_variance(roi_height, 0.0f);
    std::vector<float> col_variance(roi_width, 0.0f);

    float max_row_var = 0.0f;
    float max_col_var = 0.0f;

    for (int y = 0; y < roi_height; y++) {
        int base_y = (roi_y1 + y) * width;
        float mean = 0.0f;
        for (int x = 0; x < roi_width; x++) {
            mean += data[base_y + roi_x1 + x];
        }
        mean /= roi_width;

        float variance = 0.0f;
        for (int x = 0; x < roi_width; x++) {
            float diff = data[base_y + roi_x1 + x] - mean;
            variance += diff * diff;
        }
        row_variance[y] = variance / roi_width;
        max_row_var = std::max(max_row_var, row_variance[y]);
    }

    for (int x = 0; x < roi_width; x++) {
        float mean = 0.0f;
        for (int y = 0; y < roi_height; y++) {
            mean += data[(roi_y1 + y) * width + roi_x1 + x];
        }
        mean /= roi_height;

        float variance = 0.0f;
        for (int y = 0; y < roi_height; y++) {
            float diff = data[(roi_y1 + y) * width + roi_x1 + x] - mean;
            variance += diff * diff;
        }
        col_variance[x] = variance / roi_height;
        max_col_var = std::max(max_col_var, col_variance[x]);
    }

    int edge_top = -1, edge_bottom = -1, edge_left = -1, edge_right = -1;
    float variance_threshold = 2000.0f;

    for (int y = 0; y < roi_height; y++) {
        if (row_variance[y] > variance_threshold) {
            if (edge_top < 0) edge_top = y;
            edge_bottom = y;
        }
    }

    for (int x = 0; x < roi_width; x++) {
        if (col_variance[x] > variance_threshold) {
            if (edge_left < 0) edge_left = x;
            edge_right = x;
        }
    }

    int detected_center_x = target_center_x;
    int detected_center_y = target_center_y;
    int detected_half_size = target_width / 2;
    float confidence = 0.0f;

    if (edge_top >= 0 && edge_bottom >= 0 && edge_left >= 0 && edge_right >= 0) {
        int detected_top = roi_y1 + edge_top;
        int detected_bottom = roi_y1 + edge_bottom;
        int detected_left = roi_x1 + edge_left;
        int detected_right = roi_x1 + edge_right;

        int measured_height = detected_bottom - detected_top + 1;
        int measured_width = detected_right - detected_left + 1;

        float size_ratio = std::min(measured_height, measured_width) / static_cast<float>(std::max(measured_height, measured_width));

        float center_x_error = std::abs((detected_left + detected_right) / 2.0f - target_center_x);
        float center_y_error = std::abs((detected_top + detected_bottom) / 2.0f - target_center_y);

        if (size_ratio > 0.6f && center_x_error < target_width * 0.5f && center_y_error < target_height * 0.5f) {
            detected_center_x = (detected_left + detected_right) / 2;
            detected_center_y = (detected_top + detected_bottom) / 2;
            detected_half_size = std::max(measured_width, measured_height) / 2;

            float avg_variance = 0.0f;
            int count = 0;
            for (int y = detected_top; y <= detected_bottom && y < height; y++) {
                for (int x = detected_left; x <= detected_right && x < width; x++) {
                    avg_variance += row_variance[y - roi_y1];
                    count++;
                }
            }
            avg_variance = count > 0 ? avg_variance / count : 0.0f;
            confidence = std::min(1.0f, avg_variance / 2000.0f);

            gpu_result.target_count = 1;
        }
    }

    if (gpu_result.target_count == 1) {
        gpu_result.centers[0][0] = static_cast<float>(detected_center_x) / width;
        gpu_result.centers[0][1] = static_cast<float>(detected_center_y) / height;
        gpu_result.radii[0] = static_cast<float>(detected_half_size * 2) / std::min(width, height);
        gpu_result.confidence[0] = std::max(0.3f, confidence);
    }

    auto end_time = std::chrono::steady_clock::now();
    gpu_result.processing_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();

    if (gpu_result.target_count > 0) {
        results.push_back(gpu_result);
    }

    if (detection_callback_ && !results.empty()) {
        detection_callback_(results);
    }

    return true;
}

} // namespace inference
} // namespace aurore
