#include "frame_analyzer.h"
#include "util_logging.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

FrameAnalyzer::FrameAnalyzer(int width, int height)
    : width_(width), height_(height),
      frame_size_bytes_(static_cast<size_t>(width) * height * 3),
      quarter_frame_size_bytes_(frame_size_bytes_ / 4) {
    (void)height;
    size_t half_height = height / 2;
    (void)half_height;
    top_ref_pixels_prev_.resize(9);
    bottom_ref_pixels_prev_.resize(9);
    left_ref_pixels_prev_.resize(9);
    right_ref_pixels_prev_.resize(9);
}

float FrameAnalyzer::calculate_change_percent(const std::vector<uint8_t>& current, const std::vector<uint8_t>& previous) {
    if (previous.empty() || current.size() != previous.size()) {
        return 0.0f;
    }
    
    int changed = 0;
    for (size_t i = 0; i < current.size(); ++i) {
        if (current[i] != previous[i]) {
            changed++;
        }
    }
    
    return static_cast<float>(changed) / current.size() * 100.0f;
}

FrameAnalyzer::FrameAnalysisResult FrameAnalyzer::analyze_frame(
    const uint8_t* bgr_data, uint64_t frame_counter, 
    std::chrono::steady_clock::time_point capture_time) {
    
    FrameAnalysisResult result;
    result.frame_counter = frame_counter;
    result.capture_time = capture_time;
    result.integrity_ok = true;
    result.change_detected = false;
    result.top_half_change_percent = 0.0f;
    result.bottom_half_change_percent = 0.0f;

    if (!bgr_data) {
        APP_LOG_ERROR("FrameAnalyzer: Received null bgr_data.");
        result.integrity_ok = false;
        result.message = "Null frame data";
        last_integrity_ok_ = false;
        return result;
    }

    size_t half_height = height_ / 2;
    size_t half_frame_bytes = static_cast<size_t>(width_) * half_height * 3;

    const uint8_t* top_half_data = bgr_data;
    const uint8_t* bottom_half_data = bgr_data + half_frame_bytes;

    last_crc32_ = aurore::integrity::calculate_crc32(bgr_data, frame_size_bytes_);
    result.crc32 = last_crc32_;

    std::vector<uint8_t> current_top_ref_pixels = extract_ref_pixels(top_half_data, width_, half_height);
    std::vector<uint8_t> current_bottom_ref_pixels = extract_ref_pixels(bottom_half_data, width_, half_height);

    result.top_half_change_percent = calculate_change_percent(current_top_ref_pixels, top_ref_pixels_prev_);
    result.bottom_half_change_percent = calculate_change_percent(current_bottom_ref_pixels, bottom_ref_pixels_prev_);

    bool top_changed = result.top_half_change_percent > 0.0f;
    bool bottom_changed = result.bottom_half_change_percent > 0.0f;
    result.change_detected = top_changed || bottom_changed;

    std::stringstream ss;
    ss << "Frame " << frame_counter << " (CRC32: 0x" << std::hex << std::setw(8) << std::setfill('0') << last_crc32_ << std::dec << "): ";
    ss << "Top=" << (top_changed ? "CHANGED" : "STALE") << "(" << result.top_half_change_percent << "%) ";
    ss << "Bottom=" << (bottom_changed ? "CHANGED" : "STALE") << "(" << result.bottom_half_change_percent << "%)";
    result.message = ss.str();

    if (frame_counter <= last_frame_counter_) {
        APP_LOG_WARNING("FrameAnalyzer: Frame counter regressed from " + std::to_string(last_frame_counter_) + " to " + std::to_string(frame_counter));
        result.integrity_ok = false;
    }

    last_frame_counter_ = frame_counter;
    last_integrity_ok_ = result.integrity_ok;

    top_ref_pixels_prev_ = current_top_ref_pixels;
    bottom_ref_pixels_prev_ = current_bottom_ref_pixels;

    return result;
}

std::vector<uint8_t> FrameAnalyzer::extract_ref_pixels(const uint8_t* data, int region_width, int region_height) const {
    std::vector<uint8_t> ref_pixels;
    ref_pixels.reserve(12);

    size_t region_size = static_cast<size_t>(region_width) * region_height * 3;
    if (region_size < 12) return ref_pixels;

    size_t top_left_idx = 0;
    ref_pixels.push_back(data[top_left_idx]);
    ref_pixels.push_back(data[top_left_idx + 1]);
    ref_pixels.push_back(data[top_left_idx + 2]);

    size_t top_center_idx = (region_width / 2) * 3;
    ref_pixels.push_back(data[top_center_idx]);
    ref_pixels.push_back(data[top_center_idx + 1]);
    ref_pixels.push_back(data[top_center_idx + 2]);

    size_t top_right_idx = (region_width - 1) * 3;
    ref_pixels.push_back(data[top_right_idx]);
    ref_pixels.push_back(data[top_right_idx + 1]);
    ref_pixels.push_back(data[top_right_idx + 2]);

    size_t center_idx = (region_height / 2 * region_width + region_width / 2) * 3;
    ref_pixels.push_back(data[center_idx]);
    ref_pixels.push_back(data[center_idx + 1]);
    ref_pixels.push_back(data[center_idx + 2]);

    return ref_pixels;
}
