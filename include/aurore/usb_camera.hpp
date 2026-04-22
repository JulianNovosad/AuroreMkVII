/**
 * @file usb_camera.hpp
 * @brief USB webcam wrapper with OpenCV V4L2 backend
 *
 * Secondary camera for auxiliary tracking/logging. Uses OpenCV VideoCapture
 * with V4L2 memory-mapped I/O for near-zero-copy frame acquisition.
 *
 * Unlike CameraWrapper (libcamera/MIPI CSI-2), this targets generic USB UVC
 * webcams and trades raw DMA zero-copy for broader device compatibility.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "camera_wrapper.hpp"  // For ZeroCopyFrame, PixelFormat, CameraConfig
#include "timing.hpp"

// Forward declare OpenCV types
namespace cv {
class Mat;
class VideoCapture;
}  // namespace cv

namespace aurore {

/**
 * @brief USB camera configuration
 */
struct UsbCameraConfig {
    int width = 640;
    int height = 480;
    int fps = 30;
    int device_index = -1;              ///< V4L2 device index (-1 = auto-detect)
    std::string device_path;            ///< Explicit /dev/videoN path (overrides device_index)
    PixelFormat format = PixelFormat::BGR888;  ///< Output format (BGR888 for OpenCV)
    int buffer_count = 4;               ///< V4L2 buffer count hint

    bool validate() const noexcept {
        return width > 0 && height > 0 && fps > 0 && fps <= 120 && buffer_count >= 2;
    }
};

/**
 * @brief Camera source selector for vision pipeline switching
 */
enum class CameraSource : uint8_t {
    MIPI_CSI2 = 0,  ///< Primary MIPI CSI-2 camera (CameraWrapper)
    USB_WEBCAM = 1,  ///< Secondary USB webcam (UsbCamera)
};

/**
 * @brief USB webcam wrapper using OpenCV V4L2 backend
 *
 * Provides a ZeroCopyFrame-compatible interface so the vision pipeline
 * can switch between MIPI CSI-2 and USB cameras transparently.
 *
 * Note: "zero-copy" is best-effort here. OpenCV VideoCapture with V4L2
 * uses mmap'd buffers internally, but the retrieve() call may copy into
 * a cv::Mat. For a secondary/logging camera at 30fps this is acceptable.
 *
 * Usage:
 * @code
 *     UsbCameraConfig cfg;
 *     cfg.device_path = "/dev/video0";
 *     cfg.width = 640;
 *     cfg.height = 480;
 *
 *     UsbCamera usb_cam(cfg);
 *     if (usb_cam.init() && usb_cam.start()) {
 *         ZeroCopyFrame frame;
 *         if (usb_cam.capture_frame(frame)) {
 *             cv::Mat img = usb_cam.wrap_as_mat(frame);
 *             // Process...
 *         }
 *     }
 * @endcode
 */
class UsbCamera {
   public:
    explicit UsbCamera(const UsbCameraConfig& config = UsbCameraConfig());
    ~UsbCamera();

    // Non-copyable, non-movable (owns VideoCapture handle)
    UsbCamera(const UsbCamera&) = delete;
    UsbCamera& operator=(const UsbCamera&) = delete;

    /**
     * @brief Detect if a USB webcam is available
     *
     * Probes /dev/video* devices for a UVC-compatible camera.
     * Returns within 500ms per CLAUDE.md hardware policy.
     *
     * @return true if at least one USB webcam is detected
     */
    static bool detect() noexcept;

    /**
     * @brief Initialize camera
     *
     * Opens V4L2 device, configures resolution/fps, allocates buffers.
     *
     * @return true on success
     */
    bool init();

    /**
     * @brief Start capture
     * @return true on success
     */
    bool start();

    /**
     * @brief Stop capture and release resources
     */
    void stop();

    /**
     * @brief Check if camera is running
     */
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Capture frame into ZeroCopyFrame descriptor
     *
     * Grabs a frame from the USB camera and populates a ZeroCopyFrame
     * for compatibility with the vision pipeline ring buffer.
     *
     * Note: The frame data is held in an internal cv::Mat buffer. The
     * plane_data[0] pointer is valid until the next capture_frame() call
     * or until stop() is called.
     *
     * @param frame Output frame descriptor
     * @param timeout_ms Timeout in milliseconds (default: 100ms)
     * @return true if frame captured, false on timeout/error
     */
    bool capture_frame(ZeroCopyFrame& frame, int timeout_ms = 100);

    /**
     * @brief Wrap frame as OpenCV Mat
     *
     * Returns a cv::Mat header referencing the internal capture buffer.
     * No copy is performed.
     *
     * @param frame Frame descriptor (must have been filled by capture_frame)
     * @param target_format Ignored for USB camera (always BGR888)
     * @return cv::Mat referencing frame data
     */
    cv::Mat wrap_as_mat(const ZeroCopyFrame& frame,
                        PixelFormat target_format = PixelFormat::BGR888);

    /**
     * @brief Release frame (no-op for USB camera; buffer is reused)
     */
    void release_frame(ZeroCopyFrame& frame);

    /**
     * @brief Get camera configuration
     */
    const UsbCameraConfig& config() const noexcept { return config_; }

    /**
     * @brief Get frame count since start
     */
    uint64_t frame_count() const noexcept { return frame_count_.load(std::memory_order_acquire); }

    /**
     * @brief Get actual device path that was opened
     */
    const std::string& device_path() const noexcept { return actual_device_path_; }

   private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    UsbCameraConfig config_;
    std::string actual_device_path_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frame_count_{0};
};

}  // namespace aurore
