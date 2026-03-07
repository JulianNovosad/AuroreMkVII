#include "drm_display.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <algorithm>

static uint64_t debug_frame_counter = 0;

DrmDisplay::DrmDisplay(class Application* app) 
    : drm_fd_(-1), crtc_id_(0), connector_id_(0), fb_id_(0), buffer_handle_(0),
      mode_(), bpp_(16), pitch_(0), size_(0), map_(nullptr),
      gbm_device_(nullptr), gbm_surface_(nullptr),
      current_fb_index_(0),
      last_present_time_(0), frame_interval_us_(16667),
      total_frames_(0), failed_flips_(0), skipped_frames_(0),
      last_diag_time_(0), diag_interval_ms_(5000) {
    std::cout << "DrmDisplay constructor called" << std::endl;
}

DrmDisplay::~DrmDisplay() {
    std::cout << "DrmDisplay destructor called" << std::endl;
    print_diagnostics();
    cleanup();
}

void DrmDisplay::print_diagnostics() {
    std::cout << "\n=== DRM DISPLAY DIAGNOSTICS ===" << std::endl;
    std::cout << "Total frames presented: " << total_frames_ << std::endl;
    std::cout << "Failed page flips: " << failed_flips_ << " (" 
              << (total_frames_ > 0 ? (failed_flips_ * 100.0 / total_frames_) : 0.0) << "%)" << std::endl;
    std::cout << "Skipped frames: " << skipped_frames_ << " (" 
              << (total_frames_ > 0 ? (skipped_frames_ * 100.0 / total_frames_) : 0.0) << "%)" << std::endl;
    std::cout << "Current framebuffer: ID=" << fb_id_ << ", Size=" << size_ << " bytes" << std::endl;
    std::cout << "Display mode: " << mode_.hdisplay << "x" << mode_.vdisplay << " @ " << mode_.vrefresh << "Hz" << std::endl;
    std::cout << "===============================" << std::endl;
}

uint64_t DrmDisplay::get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

void DrmDisplay::check_system_health() {
    uint64_t now = get_time_us();
    if ((now - last_diag_time_) / 1000 >= diag_interval_ms_) {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            std::cout << "[HEALTH] Free RAM: " << (si.freeram * si.mem_unit) / (1024*1024) 
                      << " MB, Load: " << (double)si.loads[0] / (1 << SI_LOAD_SHIFT) << std::endl;
        }
        print_diagnostics();
        last_diag_time_ = now;
    }
}

bool DrmDisplay::should_present_now() {
    uint64_t now = get_time_us();
    return (now - last_present_time_) >= frame_interval_us_;
}

bool DrmDisplay::find_connector_and_crtc() {
    drmModeRes* resources = drmModeGetResources(drm_fd_);
    if (!resources) {
        std::cerr << "❌ ERROR: Failed to get DRM resources" << std::endl;
        return false;
    }

    // Find connected HDMI connector
    drmModeConnector* connected_connector = nullptr;
    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector* conn = drmModeGetConnector(drm_fd_, resources->connectors[i]);
        if (!conn) continue;
        
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connected_connector = conn;
            connector_id_ = conn->connector_id;
            std::cout << "✅ Found connected connector: ID=" << connector_id_ << std::endl;
            break;
        }
        drmModeFreeConnector(conn);
    }

    if (!connected_connector) {
        std::cerr << "❌ ERROR: No connected connector found" << std::endl;
        drmModeFreeResources(resources);
        return false;
    }

    // Get encoder and CRTC
    drmModeEncoder* encoder = drmModeGetEncoder(drm_fd_, connected_connector->encoder_id);
    if (encoder) {
        crtc_id_ = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
        std::cout << "✅ Found CRTC from encoder: ID=" << crtc_id_ << std::endl;
    }

    if (crtc_id_ == 0) {
        std::cerr << "❌ ERROR: No CRTC found" << std::endl;
        drmModeFreeConnector(connected_connector);
        drmModeFreeResources(resources);
        return false;
    }

    // Get display mode - find the closest match to requested resolution
    drmModeModeInfo* best_mode = nullptr;
    for (int i = 0; i < connected_connector->count_modes; i++) {
        drmModeModeInfo& mode = connected_connector->modes[i];
        if (best_mode == nullptr || 
            (mode.hdisplay >= 1920 && mode.vdisplay >= 1080 && 
             mode.hdisplay <= best_mode->hdisplay && mode.vdisplay <= best_mode->vdisplay)) {
            best_mode = &mode;
        }
    }
    
    if (best_mode) {
        mode_ = *best_mode;
        std::cout << "✅ Display mode: " << mode_.hdisplay << "x" << mode_.vdisplay 
                  << " @ " << mode_.vrefresh << "Hz" << std::endl;
    } else {
        std::cerr << "❌ ERROR: No suitable display mode found" << std::endl;
        drmModeFreeConnector(connected_connector);
        drmModeFreeResources(resources);
        return false;
    }

    drmModeFreeConnector(connected_connector);
    drmModeFreeResources(resources);
    return true;
}

bool DrmDisplay::create_framebuffer_pool(uint32_t width, uint32_t height) {
    std::cout << "🎨 Creating framebuffer pool (" << width << "x" << height << ")" << std::endl;
    
    for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
        Framebuffer& fb = framebuffers_[i];
        fb.in_use = false;
        fb.fb_id = 0;
        fb.map = nullptr;
        fb.dma_buf_fd = -1;

        // Create dumb buffer
        struct drm_mode_create_dumb create_req = {};
        create_req.width = width;
        create_req.height = height;
        create_req.bpp = bpp_;
        create_req.flags = 0;

        if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create_req) < 0) {
            std::cerr << "❌ Failed to create dumb buffer " << i << ": " << strerror(errno) << std::endl;
            return false;
        }

        fb.buffer_handle = create_req.handle;
        fb.pitch = create_req.pitch;
        fb.size = create_req.size;

        std::cout << "  Dumb buffer " << i << ": handle=" << fb.buffer_handle 
                  << ", pitch=" << fb.pitch << ", size=" << fb.size << std::endl;

        // Map buffer to CPU memory
        struct drm_mode_map_dumb map_req = {};
        map_req.handle = fb.buffer_handle;
        if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
            std::cerr << "❌ Failed to map dumb buffer " << i << ": " << strerror(errno) << std::endl;
            return false;
        }

        fb.map = (uint8_t*)mmap(nullptr, fb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                drm_fd_, map_req.offset);
        if (fb.map == MAP_FAILED) {
            std::cerr << "❌ mmap failed for buffer " << i << ": " << strerror(errno) << std::endl;
            fb.map = nullptr;
            return false;
        }

        std::cout << "  Mapped buffer " << i << " at " << (void*)fb.map << std::endl;

        // Add framebuffer to DRM
        uint32_t handles[4] = {fb.buffer_handle, 0, 0, 0};
        uint32_t strides[4] = {fb.pitch, 0, 0, 0};
        uint32_t offsets[4] = {0, 0, 0, 0};
        uint64_t modifiers[4] = {DRM_FORMAT_MOD_LINEAR, 0, 0, 0};

        if (drmModeAddFB2WithModifiers(drm_fd_, width, height, DRM_FORMAT_RGB565,
                                       handles, strides, offsets, modifiers,
                                       &fb.fb_id, 0) < 0) {
            std::cerr << "❌ Failed to add framebuffer " << i << ": " << strerror(errno) << std::endl;
            munmap(fb.map, fb.size);
            fb.map = nullptr;
            return false;
        }

        std::cout << "  Framebuffer " << i << ": ID=" << fb.fb_id << std::endl;
    }

    std::cout << "✅ Framebuffer pool created (" << FRAMEBUFFER_COUNT << " buffers)" << std::endl;
    return true;
}

void DrmDisplay::destroy_framebuffer_pool() {
    for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
        Framebuffer& fb = framebuffers_[i];
        if (fb.fb_id) {
            drmModeRmFB(drm_fd_, fb.fb_id);
        }
        if (fb.map) {
            munmap(fb.map, fb.size);
        }
        if (fb.dma_buf_fd >= 0) {
            close(fb.dma_buf_fd);
        }
        fb.fb_id = 0;
        fb.map = nullptr;
        fb.dma_buf_fd = -1;
        fb.in_use = false;
    }
}

Framebuffer* DrmDisplay::get_available_framebuffer() {
    for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
        int idx = (current_fb_index_ + i) % FRAMEBUFFER_COUNT;
        if (!framebuffers_[idx].in_use) {
            framebuffers_[idx].in_use = true;
            return &framebuffers_[idx];
        }
    }
    return nullptr;
}

bool DrmDisplay::import_dma_buf_as_framebuffer(int dma_fd, uint32_t width, uint32_t height, 
                                              uint32_t format, uint32_t* out_fb_id) {
    uint32_t handles[4] = {0, 0, 0, 0};
    uint32_t pitches[4] = {0, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    uint64_t modifiers[4] = {DRM_FORMAT_MOD_INVALID, 0, 0, 0};
    
    // Import DMA-BUF
    int ret = drmPrimeFDToHandle(drm_fd_, dma_fd, &handles[0]);
    if (ret) {
        std::cerr << "❌ Failed to import DMA-BUF: " << strerror(-ret) << std::endl;
        return false;
    }
    
    // Get buffer properties (this requires a bit of hack since we don't have the original buffer info)
    // In a real implementation, we'd have the pitch and format from the camera capture
    pitches[0] = width * 4; // Assuming RGBA
    offsets[0] = 0;
    format = DRM_FORMAT_ARGB8888; // Default format
    
    ret = drmModeAddFB2(drm_fd_, width, height, format,
                        handles, pitches, offsets, out_fb_id, 0);
    if (ret) {
        std::cerr << "❌ Failed to create FB from DMA-BUF: " << strerror(-ret) << std::endl;
        return false;
    }
    
    return true;
}

bool DrmDisplay::initialize(uint32_t width, uint32_t height) {
    std::cout << "🎯 INITIALIZING DRM DISPLAY: " << width << "x" << height << std::endl;
    
    // Open DRM device (try card1 first for HDMI on Pi, then card0)
    drm_fd_ = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        drm_fd_ = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (drm_fd_ < 0) {
            std::cerr << "❌ ERROR: Failed to open DRM device: " << strerror(errno) << std::endl;
            return false;
        }
    }
    std::cout << "✅ DRM device opened (FD: " << drm_fd_ << ")" << std::endl;
    
    // Create GBM device for more efficient buffer management
    gbm_device_ = gbm_create_device(drm_fd_);
    if (!gbm_device_) {
        std::cerr << "❌ Failed to create GBM device" << std::endl;
        return false;
    }
    std::cout << "✅ GBM device created" << std::endl;
    
    // Get DRM resources and find connected connector/CRTC
    if (!find_connector_and_crtc()) {
        return false;
    }
    
    // Create framebuffer pool for rendering
    if (!create_framebuffer_pool(width, height)) {
        return false;
    }
    
    // Clear all framebuffers to black
    for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
        if (framebuffers_[i].map) {
            memset(framebuffers_[i].map, 0, framebuffers_[i].size);
        }
    }

    // Set initial CRTC mode and framebuffer
    Framebuffer* first_fb = get_available_framebuffer();
    if (!first_fb) {
        std::cerr << "❌ ERROR: No available framebuffer" << std::endl;
        return false;
    }
    
    fb_id_ = first_fb->fb_id;
    int ret = drmModeSetCrtc(drm_fd_, crtc_id_, fb_id_, 0, 0, 
                             &connector_id_, 1, &mode_);
    if (ret < 0) {
        std::cerr << "❌ ERROR: Failed to set CRTC: " << strerror(errno) << std::endl;
        return false;
    }
    
    std::cout << "✅ CRTC set with initial framebuffer: " << fb_id_ << std::endl;
    
    // STARTUP SEQUENCE: Display "AURORE MK VI" for 0-0.2s
    std::cout << "🚀 Starting startup sequence..." << std::endl;
    display_startup_screen("AURORE MK VI", 0.2);
    
    // Display system status messages for 0.2-0.5s
    display_startup_screen("> VIDEO PIPELINE OK", 0.1);
    display_startup_screen("> TPU READY", 0.1);
    display_startup_screen("> BALLISTICS ACTIVE", 0.1);
    
    last_present_time_ = get_time_us();
    std::cout << "🎯 DRM DISPLAY READY" << std::endl;
    return true;
}

void DrmDisplay::present_frame(const uint8_t* frame_data, const uint8_t* overlay_data) {
    if (!frame_data) {
        std::cout << "DRM: ❌ Invalid frame data" << std::endl;
        return;
    }
    
    check_system_health();
    
    if (!should_present_now()) {
        skipped_frames_++;
        return;
    }
    
    // Get available framebuffer
    Framebuffer* fb = get_available_framebuffer();
    if (!fb) {
        std::cout << "DRM: ❌ No available framebuffer" << std::endl;
        return;
    }
    
    // The input frame is expected to be in BGR888 format from camera
    // The DRM framebuffer is RGB565 (2 bytes per pixel)
    // Display resolution is mode_.hdisplay x mode_.vdisplay
    
    uint32_t src_row_bytes = mode_.hdisplay * 3;  // BGR888 = 3 bytes/pixel
    uint32_t dst_row_bytes = mode_.hdisplay * 2;  // RGB565 = 2 bytes/pixel
    
    // Convert BGR888 to RGB565
    for (uint32_t y = 0; y < mode_.vdisplay; y++) {
        const uint8_t* src_row = frame_data + y * src_row_bytes;
        uint8_t* dst_row = fb->map + y * dst_row_bytes;
        
        for (uint32_t x = 0; x < mode_.hdisplay; x++) {
            const uint8_t* src_pixel = src_row + x * 3;
            uint8_t* dst_pixel = dst_row + x * 2;
            
            // BGR888 -> RGB565 conversion
            // Swap R and B for correct display
            uint8_t b = src_pixel[0];
            uint8_t g = src_pixel[1];
            uint8_t r = src_pixel[2];
            
            // RGB565: R=bits15-11, G=bits10-5, B=bits4-0
            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g6 = (g >> 2) & 0x3F;
            uint16_t b5 = (b >> 3) & 0x1F;
            uint16_t rgb565 = (r5 << 11) | (g6 << 5) | b5;
            
            // Little-endian storage
            dst_pixel[0] = rgb565 & 0xFF;
            dst_pixel[1] = (rgb565 >> 8) & 0xFF;
        }
    }
    
    // Draw HUD overlays on the frame
    uint32_t w = mode_.hdisplay;
    uint32_t h = mode_.vdisplay;
    
    // Draw crosshair at center (bright green)
    uint16_t crosshair_color = (31 << 11) | (63 << 5) | 0;  // Bright green RGB565
    uint32_t center_x = w / 2;
    uint32_t center_y = h / 2;
    uint32_t crosshair_size = 20;
    
    // Horizontal line
    for (uint32_t x = center_x - crosshair_size; x <= center_x + crosshair_size && x < w; x++) {
        uint8_t* pixel = fb->map + center_y * dst_row_bytes + x * 2;
        pixel[0] = crosshair_color & 0xFF;
        pixel[1] = (crosshair_color >> 8) & 0xFF;
    }
    // Vertical line
    for (uint32_t y = center_y - crosshair_size; y <= center_y + crosshair_size && y < h; y++) {
        uint8_t* pixel = fb->map + y * dst_row_bytes + center_x * 2;
        pixel[0] = crosshair_color & 0xFF;
        pixel[1] = (crosshair_color >> 8) & 0xFF;
    }
    
    // Draw FPS counter area (top-left, green text background)
    uint16_t hud_bg_color = 0x0000;  // Black
    uint16_t hud_text_color = 0x07E0;  // Green
    uint32_t hud_x = 30;
    uint32_t hud_y = 30;
    uint32_t hud_w = 200;
    uint32_t hud_h = 100;
    
    // HUD background rectangle
    for (uint32_t y = hud_y; y < hud_y + hud_h && y < h; y++) {
        for (uint32_t x = hud_x; x < hud_x + hud_w && x < w; x++) {
            uint8_t* pixel = fb->map + y * dst_row_bytes + x * 2;
            pixel[0] = hud_bg_color & 0xFF;
            pixel[1] = (hud_bg_color >> 8) & 0xFF;
        }
    }
    
    // Draw test pattern - colored stripes in HUD area
    // Red stripe
    uint16_t red_color = (31 << 11) | (0 << 5) | 0;
    for (uint32_t y = hud_y + 10; y < hud_y + 30 && y < h; y++) {
        for (uint32_t x = hud_x + 10; x < hud_x + 60 && x < w; x++) {
            uint8_t* pixel = fb->map + y * dst_row_bytes + x * 2;
            pixel[0] = red_color & 0xFF;
            pixel[1] = (red_color >> 8) & 0xFF;
        }
    }
    
    // Green stripe  
    uint16_t green_color = (0 << 11) | (63 << 5) | 0;
    for (uint32_t y = hud_y + 35; y < hud_y + 55 && y < h; y++) {
        for (uint32_t x = hud_x + 10; x < hud_x + 60 && x < w; x++) {
            uint8_t* pixel = fb->map + y * dst_row_bytes + x * 2;
            pixel[0] = green_color & 0xFF;
            pixel[1] = (green_color >> 8) & 0xFF;
        }
    }
    
    // Blue stripe
    uint16_t blue_color = (0 << 11) | (0 << 5) | 31;
    for (uint32_t y = hud_y + 60; y < hud_y + 80 && y < h; y++) {
        for (uint32_t x = hud_x + 10; x < hud_x + 60 && x < w; x++) {
            uint8_t* pixel = fb->map + y * dst_row_bytes + x * 2;
            pixel[0] = blue_color & 0xFF;
            pixel[1] = (blue_color >> 8) & 0xFF;
        }
    }
    
    // White stripe (for comparison)
    uint16_t white_color = (31 << 11) | (63 << 5) | 31;
    for (uint32_t y = hud_y + 10; y < hud_y + 80 && y < h; y++) {
        for (uint32_t x = hud_x + 140; x < hud_x + 190 && x < w; x++) {
            uint8_t* pixel = fb->map + y * dst_row_bytes + x * 2;
            pixel[0] = white_color & 0xFF;
            pixel[1] = (white_color >> 8) & 0xFF;
        }
    }
    
    // Page flip to new framebuffer (synchronous mode)
    int ret = drmModePageFlip(drm_fd_, crtc_id_, fb->fb_id, 0, nullptr);
    
    last_present_time_ = get_time_us();
    total_frames_++;
    
    if (ret == -EBUSY) {
        // Flip already pending, don't count as failure but skip
        fb->in_use = false;
        for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
            if (framebuffers_[i].fb_id != fb->fb_id) {
                framebuffers_[i].in_use = false;
            }
        }
        if (total_frames_ % 60 == 0) {
            std::cout << "ℹ️  PAGE FLIP BUSY (already pending)" << std::endl;
        }
        return;
    }
    
    if (ret != 0) {
        failed_flips_++;
        // Release all buffers on failure (especially ENOMEM)
        for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
            framebuffers_[i].in_use = false;
        }
        if (ret == -ENOMEM) {
            std::cerr << "❌ PAGE FLIP FAILED [" << failed_flips_ << "]: Out of memory - released all buffers" << std::endl;
        } else {
            std::cerr << "❌ PAGE FLIP FAILED [" << failed_flips_ << "]: " << strerror(-ret) << std::endl;
        }
    } else {
        // Release old framebuffers after successful page flip
        for (int i = 0; i < FRAMEBUFFER_COUNT; i++) {
            if (framebuffers_[i].fb_id != fb->fb_id) {
                framebuffers_[i].in_use = false;
            }
        }
        if (total_frames_ % 60 == 0) {
            std::cout << "✅ PAGE FLIP SUCCESS [" << total_frames_ << "]: FB_ID=" << fb->fb_id << std::endl;
        }
    }
}

void DrmDisplay::render_frame(const uint8_t* frame_data, uint32_t frame_width, uint32_t frame_height) {
    present_frame(frame_data, nullptr);
}

void DrmDisplay::display_startup_screen(const std::string& text, double duration_seconds) {
    Framebuffer* fb = get_available_framebuffer();
    if (!fb) {
        std::cerr << "❌ No available framebuffer for startup screen" << std::endl;
        return;
    }
    
    // Clear framebuffer to black
    memset(fb->map, 0, fb->size);
    
    // Calculate centered position for text
    uint32_t width = mode_.hdisplay;
    uint32_t height = mode_.vdisplay;
    int text_width = text.length() * 8;  // Approximate character width
    int x = (width - text_width) / 2;
    int y = height / 2;
    
    // Draw text in phosphor green (RGB565: R=0, G=63, B=0 = 0x07E0)
    for (const char* p = text.c_str(); *p && x < (int)width; p++, x += 8) {
        for (int py = y + 4; py < y + 20 - 4 && py < (int)height; py++) {
            uint8_t* pixel = fb->map + py * width * 2 + x * 2;
            if (pixel && py * width * 2 + x * 2 + 1 < fb->size) {
                pixel[0] = 0xE0;  // Low byte of 0x07E0 (G=63)
                pixel[1] = 0x07;  // High byte of 0x07E0 (G=63)
            }
        }
    }
    
    // Draw HUD test: green rectangle at top-left corner
    uint16_t hud_test_color = 0x07E0;  // Green in RGB565
    for (int hud_y = 30; hud_y < 80 && hud_y < (int)height; hud_y++) {
        uint8_t* hud_row = fb->map + hud_y * width * 2;
        for (int hud_x = 30; hud_x < 150 && hud_x < (int)width; hud_x++) {
            uint8_t* hud_pixel = hud_row + hud_x * 2;
            if (hud_pixel && hud_y * width * 2 + hud_x * 2 + 1 < fb->size) {
                hud_pixel[0] = hud_test_color & 0xFF;
                hud_pixel[1] = (hud_test_color >> 8) & 0xFF;
            }
        }
    }
    
    // Page flip to display the startup screen (synchronous)
    int ret = drmModePageFlip(drm_fd_, crtc_id_, fb->fb_id, 0, nullptr);
    if (ret != 0) {
        std::cerr << "❌ Startup screen page flip failed: " << strerror(-ret) << std::endl;
    }
    
    // Wait for the specified duration
    usleep(static_cast<useconds_t>(duration_seconds * 1000000));
    
    // Release framebuffer for reuse
    fb->in_use = false;
}

void DrmDisplay::cleanup() {
    std::cout << "🧹 Cleaning up DRM resources..." << std::endl;
    
    destroy_framebuffer_pool();
    
    if (gbm_device_) {
        gbm_device_destroy(gbm_device_);
        gbm_device_ = nullptr;
    }
    
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
    
    std::cout << "✅ DRM cleanup complete" << std::endl;
}