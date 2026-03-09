/**
 * @file image_preprocessor.hpp
 * @brief RAW10→BGR888 conversion and image preprocessing for vision pipeline
 *
 * Converts Sony IMX708 RAW10 Bayer format to BGR888 for OpenCV processing.
 *
 * RAW10 format: 10-bit pixels packed into 16-bit words
 * - 4 pixels packed into 5 bytes: AAAAAAAA AABBBBBB BBBBCCCC CCCCCCDD DDDDDDDD
 * - Layout: LSB to MSB (little-endian 16-bit words)
 *
 * Bayer pattern (IMX708):
 * - GRBG: Row 0 starts with GR, Row 1 starts with BG
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace cv {
    class Mat;
}

namespace aurore {

/**
 * @brief Image preprocessing exception
 */
class ImagePreprocessorException : public std::runtime_error {
public:
    explicit ImagePreprocessorException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/**
 * @brief RAW10 to BGR888 converter
 *
 * Handles RAW10 unpacking and Bayer demosaicing.
 * Designed for real-time processing with minimal overhead.
 */
class ImagePreprocessor {
public:
    /**
     * @brief Construct preprocessor for given resolution
     *
     * @param width Frame width
     * @param height Frame height
     */
    ImagePreprocessor(int width, int height);

    ~ImagePreprocessor() = default;

    // Non-copyable
    ImagePreprocessor(const ImagePreprocessor&) = delete;
    ImagePreprocessor& operator=(const ImagePreprocessor&) = delete;

    /**
     * @brief Convert RAW10 buffer to BGR888 OpenCV Mat
     *
     * Takes RAW10 DMA buffer and produces BGR888 Mat suitable for ORB detection.
     *
     * @param raw10_data Pointer to RAW10 packed data
     * @param raw10_stride Bytes per line (including padding)
     * @return cv::Mat BGR888 image (24-bit color)
     * @throws ImagePreprocessorException on invalid input
     */
    cv::Mat convert_raw10_to_bgr888(const void* raw10_data, int raw10_stride);

    /**
     * @brief Apply simple color threshold for target segmentation
     *
     * Useful for separating targets from background via HSV color range.
     *
     * @param bgr_input BGR image
     * @param hsv_min_h Hue min (0-180)
     * @param hsv_max_h Hue max (0-180)
     * @param hsv_min_s Saturation min (0-255)
     * @param hsv_max_s Saturation max (0-255)
     * @param hsv_min_v Value min (0-255)
     * @param hsv_max_v Value max (0-255)
     * @return cv::Mat Binary mask (CV_8UC1)
     */
    cv::Mat threshold_hsv(const cv::Mat& bgr_input,
                          int hsv_min_h, int hsv_max_h,
                          int hsv_min_s, int hsv_max_s,
                          int hsv_min_v, int hsv_max_v);

    /**
     * @brief Get preprocessor resolution
     */
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    int width_;
    int height_;

    /**
     * @brief Unpack 4 RAW10 pixels from 5-byte buffer
     *
     * RAW10 packing (5 bytes for 4 10-bit pixels):
     *   Byte 0: AAAAAAAA (pixel 0, bits 9-2)
     *   Byte 1: AABBBBBB (pixel 0 bits 1-0, pixel 1 bits 9-4)
     *   Byte 2: BBBBCCCC (pixel 1 bits 3-0, pixel 2 bits 9-6)
     *   Byte 3: CCCCCCDD (pixel 2 bits 5-0, pixel 3 bits 9-8)
     *   Byte 4: DDDDDDDD (pixel 3 bits 7-0)
     *
     * @param src 5-byte RAW10 packet
     * @param pix Output array for 4 16-bit pixels
     */
    static void unpack_raw10_pixel_quad(const uint8_t* src, uint16_t* pix) noexcept;

    /**
     * @brief Simple bilinear Bayer demosaicing
     *
     * Converts Bayer RAW to RGB using bilinear interpolation.
     * GRBG pattern (IMX708).
     *
     * @param raw_row0 Current row RAW pixels
     * @param raw_row1 Next row RAW pixels (or nullptr for last row)
     * @param rgb_out Output RGB triplets
     * @param width Width in pixels
     */
    static void demosaic_row_grbg(const uint16_t* raw_row0,
                                   const uint16_t* raw_row1,
                                   uint8_t* rgb_out,
                                   int width) noexcept;
};

}  // namespace aurore
