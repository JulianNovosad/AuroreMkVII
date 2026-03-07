#ifndef FRAME_ANALYZER_H
#define FRAME_ANALYZER_H

#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include "data_integrity.h"

class FrameAnalyzer {
public:
    FrameAnalyzer(int width, int height);

    struct FrameAnalysisResult {
        bool integrity_ok;
        uint32_t crc32;
        bool change_detected;
        float top_half_change_percent;
        float bottom_half_change_percent;
        uint64_t frame_counter;
        std::chrono::steady_clock::time_point capture_time;
        std::string message;
    };

    FrameAnalysisResult analyze_frame(const uint8_t* bgr_data, uint64_t frame_counter, 
                                       std::chrono::steady_clock::time_point capture_time);
    
    uint32_t get_last_crc32() const { return last_crc32_; }
    bool get_last_integrity_ok() const { return last_integrity_ok_; }

private:
    int width_;
    int height_;
    size_t frame_size_bytes_;
    size_t quarter_frame_size_bytes_;

    uint32_t last_crc32_ = 0;
    bool last_integrity_ok_ = true;
    uint64_t last_frame_counter_ = 0;

    std::vector<uint8_t> top_ref_pixels_prev_;
    std::vector<uint8_t> bottom_ref_pixels_prev_;
    std::vector<uint8_t> left_ref_pixels_prev_;
    std::vector<uint8_t> right_ref_pixels_prev_;

    float calculate_change_percent(const std::vector<uint8_t>& current, const std::vector<uint8_t>& previous);
    std::vector<uint8_t> extract_ref_pixels(const uint8_t* data, int region_width, int region_height) const;
};

#endif // FRAME_ANALYZER_H
