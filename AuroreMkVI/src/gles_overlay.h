#pragma once

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>  // For glEGLImageTargetTexture2DOES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/dma-buf.h>

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>

#include "pipeline_structs.h"
#include "util_logging.h"
#include "config_loader.h"

// Forward declaration
struct OverlayData;

/**
 * @brief GPU-accelerated overlay composition using OpenGL ES
 * 
 * This class handles composition of camera feed with detection overlays using OpenGL ES
 * instead of Vulkan, which is more compatible with Raspberry Pi 5's GPU drivers.
 */
class GlesOverlay {
public:
    GlesOverlay(int width, int height);
    ~GlesOverlay();

    bool initialize(int drm_fd);
    void render(int camera_dma_buf_fd, size_t dma_buf_size, uint32_t frame_id, const OverlayData& overlay_data);
    void render_from_cpu_data(const uint8_t* cpu_frame_data, uint32_t width, uint32_t height, uint32_t frame_id, const OverlayData& overlay_data);
    int get_rendered_dma_buf_fd();  // This will handle buffer release after returning the FD
    void release_buffer_object();   // Call this after the display has used the DMA-BUF FD

    // Get GBM buffer properties for DRM import
    uint32_t get_gbm_buffer_format() const;
    uint32_t get_gbm_buffer_stride() const;
    uint32_t get_gbm_buffer_handle() const;

    // Getters for application access
    int get_width() const { return width_; }
    int get_height() const { return height_; }

    // Get the GBM surface for direct presentation
    gbm_surface* get_gbm_surface() const { return gbm_surface_; }
    
    // Release EGL context so other threads can use it
    void release_context();

private:
    int width_;
    int height_;
    float tpu_temperature_;

    // EGL resources
    EGLDisplay egl_display_;
    EGLContext egl_context_;
    EGLConfig egl_config_;
    EGLSurface egl_surface_;

    // GBM resources (shared with DrmDisplay)
    gbm_device* gbm_device_;
    gbm_surface* gbm_surface_;
    gbm_bo* gbm_bo_;
    
    // OpenGL ES resources
    GLuint program_;
    GLuint vertex_shader_;
    GLuint fragment_shader_;
    GLuint vao_;
    GLuint vbo_;
    GLuint ebo_;
    
    // Textures
    GLuint camera_texture_;  // For camera feed
    GLuint overlay_texture_; // For overlay data (if needed as texture)

    // Framebuffer for offscreen rendering
    GLuint framebuffer_;
    GLuint color_renderbuffer_; // Color renderbuffer attached to framebuffer
    
    // DMA-BUF handling
    int input_dma_buf_fd_;
    int output_dma_buf_fd_;
    size_t dma_buf_size_;
    uint32_t output_gem_handle_;
    
    // Shader attribute/uniform locations
    GLint pos_attr_;
    GLint tex_coord_attr_;
    GLint overlay_data_uniform_; // For passing overlay information
    
    // Dimensions and state
    uint32_t frame_id_;
    bool initialized_;
    
    // For monitoring
    std::chrono::steady_clock::time_point last_render_time_;
    uint64_t total_frames_;
    uint64_t failed_renders_;
    
    // Private helper methods
    bool create_egl_context();
    bool create_gbm_surface();
    bool create_opengl_resources();
    bool create_shader_program();
    bool create_vertex_buffer();
    bool create_framebuffer();
    bool create_textures();
    void destroy_resources();
    
    // DMA-BUF import/export
    bool import_dma_buf_as_texture(int dma_buf_fd, size_t size, uint32_t width, uint32_t height, GLuint* out_texture);
    bool export_framebuffer_as_dma_buf(int* out_fd, size_t* out_size);
    
    // Rendering helpers
    void render_frame(const OverlayData& overlay_data);
    void setup_render_state();
    void cleanup_render_state();
    
    // Overlay drawing functions
    void draw_detection_boxes(const std::vector<BoundingBoxOverlay>& detections);
    void draw_ballistic_point(const OverlayBallisticPoint& ballistic_point);
};
