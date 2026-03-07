#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include <string>

struct Framebuffer {
    uint32_t fb_id;
    uint32_t buffer_handle;
    uint8_t* map;
    uint32_t size;
    uint32_t pitch;
    bool in_use;
    int dma_buf_fd;  // For zero-copy access
};

class Application;

class DrmDisplay {
public:
    DrmDisplay(class Application* app = nullptr);
    ~DrmDisplay();
    
    bool initialize(uint32_t width, uint32_t height);
    void present_frame(const uint8_t* frame_data, const uint8_t* overlay_data);
    void render_frame(const uint8_t* frame_data, uint32_t frame_width, uint32_t frame_height);
    void cleanup();
    void print_diagnostics();
    
    // Getters for application access
    int get_fd() const { return drm_fd_; }
    uint32_t get_width() const { return mode_.hdisplay; }
    uint32_t get_height() const { return mode_.vdisplay; }
    uint32_t get_pitch() const { return pitch_; }
    uint32_t get_bpp() const { return bpp_; }
    
private:
    int drm_fd_;
    uint32_t crtc_id_;
    uint32_t connector_id_;
    uint32_t fb_id_;
    uint32_t buffer_handle_;
    drmModeModeInfo mode_;
    uint32_t bpp_;
    uint32_t pitch_;
    uint32_t size_;
    uint8_t* map_;
    
    // GBM for more efficient buffer management
    struct gbm_device* gbm_device_;
    struct gbm_surface* gbm_surface_;
    
    // Framebuffer pool (2 buffers for double buffering - more memory efficient)
    static const int FRAMEBUFFER_COUNT = 2;
    Framebuffer framebuffers_[FRAMEBUFFER_COUNT];
    int current_fb_index_;
    
    // Timing and diagnostics
    uint64_t last_present_time_;
    uint64_t frame_interval_us_;
    uint64_t total_frames_;
    uint64_t failed_flips_;
    uint64_t skipped_frames_;
    
    // Health monitoring
    uint64_t last_diag_time_;
    uint32_t diag_interval_ms_;
    
    bool find_connector_and_crtc();
    bool create_framebuffer_pool(uint32_t width, uint32_t height);
    void destroy_framebuffer_pool();
    Framebuffer* get_available_framebuffer();
    bool setup_mode();
    uint64_t get_time_us();
    bool should_present_now();
    void check_system_health();
    void display_startup_screen(const std::string& text, double duration_seconds);
    
    // Methods for DMA-BUF import
    bool import_dma_buf_as_framebuffer(int dma_fd, uint32_t width, uint32_t height, uint32_t format, uint32_t* out_fb_id);
};

#endif // DRM_DISPLAY_H