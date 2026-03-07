#ifndef GPU_DETECTOR_H
#define GPU_DETECTOR_H

#include "gles_utils.h"
#include "../pipeline_structs.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>

namespace aurore {
namespace inference {

class GPUDetector {
public:
    GPUDetector();
    ~GPUDetector();

    bool init(int frame_width, int frame_height);
    void destroy();

    bool process_frame(const ImageData& image_data, std::vector<GPUDetectionResult>& results);

    void set_enabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    bool is_enabled() const { return enabled_.load(std::memory_order_acquire); }

    bool is_available() const { return available_.load(std::memory_order_acquire); }
    bool is_software_mode() const { return software_mode_; }

    int get_detections_per_second() const { return detections_per_sec_.load(std::memory_order_relaxed); }
    float get_processing_time_us() const { return processing_time_us_.load(std::memory_order_relaxed); }
    int get_failed_frames() const { return failed_frames_.load(std::memory_order_relaxed); }

    using DetectionCallback = std::function<void(const std::vector<GPUDetectionResult>&)>;
    void set_detection_callback(DetectionCallback callback) {
        detection_callback_ = std::move(callback);
    }

private:
    bool init_compute_pipelines(int width, int height);
    bool upload_frame(const ImageData& image_data);
    void run_8pass_pipeline();
    void read_detection_results(std::vector<GPUDetectionResult>& results);
    bool process_frame_software(const ImageData& image_data, std::vector<GPUDetectionResult>& results);

    int frame_width_;
    int frame_height_;
    bool initialized_;
    std::atomic<bool> available_;
    std::atomic<bool> enabled_;
    std::atomic<int> detections_per_sec_;
    std::atomic<float> processing_time_us_;
    std::atomic<int> failed_frames_;
    bool software_mode_;

    aurore::gpu::GLContext& gl_ctx_;
    aurore::gpu::GLTexturePool texture_pool_;
    GLuint vao_;
    GLuint vbo_;

    GLuint camera_texture_;
    GLuint intermediate_texture_;
    GLuint binary_texture_;
    GLuint filtered_texture_;
    GLuint morph_texture_;
    GLuint edge_texture_;
    GLuint accum_texture_;
    GLuint centroid_texture_;
    GLuint result_texture_;
    GLuint framebuffer_;
    GLuint program_;

    GLuint programs_[8];
    GLuint framebuffers_[6];

    GLint u_camera_loc_;
    GLint u_frame_size_loc_;
    GLint u_center_x_loc_;
    GLint u_center_y_loc_;
    GLint u_max_radius_loc_;

    float pass_times_[9];
    int current_pass_;

    std::vector<uint8_t> pixel_buffer_;
    std::vector<GPUDetectionResult> pending_results_;

    DetectionCallback detection_callback_;

    std::chrono::steady_clock::time_point last_fps_update_;
    int frame_count_;
    int detection_count_;
};

} // namespace inference
} // namespace aurore

#endif // GPU_DETECTOR_H
