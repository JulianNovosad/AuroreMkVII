#include "fbdev_display.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <algorithm>

static uint64_t debug_frame_counter = 0;

FbdevDisplay::FbdevDisplay()
    : fb_fd_(-1),
      fb_ptr_(nullptr),
      screen_size_(0),
      width_(1280),
      height_(720),
      bpp_(16),
      pitch_(0),
      total_frames_(0),
      skipped_frames_(0),
      last_present_time_(0),
      frame_interval_us_(16667),
      last_diag_time_(0),
      diag_interval_ms_(5000) {
    std::cout << "FbdevDisplay constructor called" << std::endl;
}

FbdevDisplay::~FbdevDisplay() {
    std::cout << "FbdevDisplay destructor called" << std::endl;
    print_diagnostics();
    cleanup();
}

void FbdevDisplay::print_diagnostics() {
    std::cout << "\n=== FBDEV DISPLAY DIAGNOSTICS ===" << std::endl;
    std::cout << "Total frames presented: " << total_frames_ << std::endl;
    std::cout << "Skipped frames: " << skipped_frames_ << std::endl;
    std::cout << "Display resolution: " << width_ << "x" << height_ << std::endl;
    std::cout << "Bits per pixel: " << bpp_ << std::endl;
    std::cout << "Pitch: " << pitch_ << std::endl;
    std::cout << "===============================" << std::endl;
}

uint64_t FbdevDisplay::get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

bool FbdevDisplay::initialize() {
    std::cout << "🎯 INITIALIZING FBDEV DISPLAY" << std::endl;

    // Open framebuffer device
    fb_fd_ = open("/dev/fb0", O_RDWR);
    if (fb_fd_ < 0) {
        std::cerr << "❌ ERROR: Failed to open /dev/fb0: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "✅ Opened /dev/fb0 (FD: " << fb_fd_ << ")" << std::endl;

    // Get framebuffer fixed information
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb_fd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
        std::cerr << "❌ ERROR: Failed to get fixed screeninfo: " << strerror(errno) << std::endl;
        close(fb_fd_);
        return false;
    }
    std::cout << "✅ Framebuffer fix: " << finfo.id << std::endl;
    std::cout << "  Line length (pitch): " << finfo.line_length << std::endl;
    std::cout << "  Memory size: " << finfo.smem_len << " bytes" << std::endl;

    pitch_ = finfo.line_length;

    // Get framebuffer variable information
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        std::cerr << "❌ ERROR: Failed to get variable screeninfo: " << strerror(errno) << std::endl;
        close(fb_fd_);
        return false;
    }
    std::cout << "✅ Framebuffer var: " << vinfo.xres << "x" << vinfo.yres << ", "
              << vinfo.bits_per_pixel << " bpp" << std::endl;

    width_ = vinfo.xres;
    height_ = vinfo.yres;
    bpp_ = vinfo.bits_per_pixel;

    // Calculate screen size
    screen_size_ = pitch_ * height_;
    std::cout << "  Screen size: " << screen_size_ << " bytes" << std::endl;

    // Memory map the framebuffer
    fb_ptr_ = (uint8_t*)mmap(NULL, screen_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd_, 0);
    if (fb_ptr_ == MAP_FAILED) {
        std::cerr << "❌ ERROR: Failed to mmap framebuffer: " << strerror(errno) << std::endl;
        close(fb_fd_);
        return false;
    }
    std::cout << "✅ Framebuffer mmap'd at " << (void*)fb_ptr_ << std::endl;

    // Clear framebuffer to black
    memset(fb_ptr_, 0, screen_size_);
    std::cout << "✅ Framebuffer cleared" << std::endl;

    // Display startup screen
    display_startup_screen("AURORE MK VI", 0.2);

    last_present_time_ = get_time_us();
    std::cout << "🎯 FBDEV DISPLAY READY" << std::endl;
    return true;
}

void FbdevDisplay::render_frame(const uint8_t* frame_data, uint32_t frame_width, uint32_t frame_height) {
    if (!fb_ptr_ || !frame_data) {
        std::cerr << "FBDEV: Invalid frame data or not initialized" << std::endl;
        return;
    }

    check_system_health();

    if (!should_present_now()) {
        skipped_frames_++;
        return;
    }

    // Copy frame to framebuffer
    // Frame data is expected in RGB888 format (3 bytes per pixel)
    // Framebuffer is in RGB565 format (2 bytes per pixel) for Pi 5
    uint32_t copy_width = std::min(frame_width, width_);
    uint32_t copy_height = std::min(frame_height, height_);

    for (uint32_t y = 0; y < copy_height; y++) {
        const uint8_t* src_row = frame_data + y * frame_width * 3;
        uint8_t* dst_row = fb_ptr_ + y * pitch_;

        for (uint32_t x = 0; x < copy_width; x++) {
            const uint8_t* src_pixel = src_row + x * 3;
            uint8_t* dst_pixel = dst_row + x * 2;

            // RGB888 -> RGB565 conversion
            uint8_t r = src_pixel[0];
            uint8_t g = src_pixel[1];
            uint8_t b = src_pixel[2];

            uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            dst_pixel[0] = rgb565 & 0xFF;
            dst_pixel[1] = (rgb565 >> 8) & 0xFF;
        }
    }

    // Draw HUD overlay (simple crosshair)
    draw_hud_overlay();

    last_present_time_ = get_time_us();
    total_frames_++;

    if (total_frames_ % 60 == 0) {
        std::cout << "✅ FBDEV: Frame " << total_frames_ << " presented" << std::endl;
    }
}

void FbdevDisplay::draw_hud_overlay() {
    // Draw crosshair at center (bright green)
    uint16_t crosshair_color = 0x07E0;  // Green in RGB565
    uint32_t center_x = width_ / 2;
    uint32_t center_y = height_ / 2;
    uint32_t crosshair_size = 20;

    // Horizontal line
    for (uint32_t x = center_x - crosshair_size; x <= center_x + crosshair_size && x < width_; x++) {
        uint8_t* pixel = fb_ptr_ + center_y * pitch_ + x * 2;
        pixel[0] = crosshair_color & 0xFF;
        pixel[1] = (crosshair_color >> 8) & 0xFF;
    }
    // Vertical line
    for (uint32_t y = center_y - crosshair_size; y <= center_y + crosshair_size && y < height_; y++) {
        uint8_t* pixel = fb_ptr_ + y * pitch_ + center_x * 2;
        pixel[0] = crosshair_color & 0xFF;
        pixel[1] = (crosshair_color >> 8) & 0xFF;
    }
}

void FbdevDisplay::display_startup_screen(const std::string& text, double duration_seconds) {
    if (!fb_ptr_) return;

    // Clear to black
    memset(fb_ptr_, 0, screen_size_);

    // Draw centered text (using simple pixel drawing for now)
    uint32_t center_x = width_ / 2;
    uint32_t center_y = height_ / 2;

    // Draw a green crosshair at center
    uint16_t color = 0x07E0;  // Green in RGB565
    for (int i = -50; i <= 50; i++) {
        if (center_x + i < width_) {
            uint8_t* p1 = fb_ptr_ + center_y * pitch_ + (center_x + i) * 2;
            p1[0] = color & 0xFF;
            p1[1] = (color >> 8) & 0xFF;
        }
        if (center_y + i < height_) {
            uint8_t* p2 = fb_ptr_ + (center_y + i) * pitch_ + center_x * 2;
            p2[0] = color & 0xFF;
            p2[1] = (color >> 8) & 0xFF;
        }
    }

    usleep(static_cast<useconds_t>(duration_seconds * 1000000));
}

void FbdevDisplay::cleanup() {
    std::cout << "🧹 Cleaning up FBDEV resources..." << std::endl;

    if (fb_ptr_ && fb_ptr_ != MAP_FAILED) {
        // Clear framebuffer on exit
        memset(fb_ptr_, 0, screen_size_);
        munmap(fb_ptr_, screen_size_);
        fb_ptr_ = nullptr;
    }

    if (fb_fd_ >= 0) {
        close(fb_fd_);
        fb_fd_ = -1;
    }

    std::cout << "✅ FBDEV cleanup complete" << std::endl;
}

void FbdevDisplay::check_system_health() {
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

bool FbdevDisplay::should_present_now() {
    uint64_t now = get_time_us();
    return (now - last_present_time_) >= frame_interval_us_;
}
