#ifndef FBDEV_DISPLAY_H
#define FBDEV_DISPLAY_H

#include <cstdint>
#include <string>

class FbdevDisplay {
public:
    FbdevDisplay();
    ~FbdevDisplay();

    bool initialize();
    void render_frame(const uint8_t* frame_data, uint32_t frame_width, uint32_t frame_height);
    void cleanup();
    void print_diagnostics();

    // Getters
    int get_fd() const { return fb_fd_; }
    uint32_t get_width() const { return width_; }
    uint32_t get_height() const { return height_; }
    uint32_t get_pitch() const { return pitch_; }
    uint32_t get_bpp() const { return bpp_; }

private:
    int fb_fd_;
    uint8_t* fb_ptr_;
    size_t screen_size_;
    uint32_t width_;
    uint32_t height_;
    uint32_t bpp_;
    uint32_t pitch_;

    // Timing
    uint64_t last_present_time_;
    uint64_t frame_interval_us_;
    uint64_t total_frames_;
    uint64_t skipped_frames_;

    // Diagnostics
    uint64_t last_diag_time_;
    uint32_t diag_interval_ms_;

    // Private methods
    uint64_t get_time_us();
    bool should_present_now();
    void check_system_health();
    void display_startup_screen(const std::string& text, double duration_seconds);
    void draw_hud_overlay();
};

#endif // FBDEV_DISPLAY_H
