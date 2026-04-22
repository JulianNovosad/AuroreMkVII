/**
 * @file usb_camera.cpp
 * @brief USB webcam driver using OpenCV V4L2 backend
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include "aurore/usb_camera.hpp"
#include "aurore/timing.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

namespace aurore {

// ============================================================================
// Implementation (pimpl)
// ============================================================================

struct UsbCamera::Impl {
    cv::VideoCapture capture;
    cv::Mat current_frame;  // Internal buffer for last captured frame
};

// ============================================================================
// Static detection
// ============================================================================

bool UsbCamera::detect() noexcept {
    // Probe /dev/video0 through /dev/video9 for UVC webcams
    for (int i = 0; i < 10; ++i) {
        std::string path = "/dev/video" + std::to_string(i);
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct v4l2_capability cap {};
        bool is_usb_cam = false;

        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            // Must support video capture and streaming
            if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                (cap.capabilities & V4L2_CAP_STREAMING)) {
                // Filter out MIPI CSI-2 cameras (bcm2835/unicam/rp1 drivers)
                const char* driver = reinterpret_cast<const char*>(cap.driver);
                if (std::strstr(driver, "uvcvideo") != nullptr) {
                    is_usb_cam = true;
                }
            }
        }

        ::close(fd);
        if (is_usb_cam) return true;
    }
    return false;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

UsbCamera::UsbCamera(const UsbCameraConfig& config) : impl_(std::make_unique<Impl>()), config_(config) {}

UsbCamera::~UsbCamera() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool UsbCamera::init() {
    if (!config_.validate()) {
        std::cerr << "[UsbCamera] Invalid configuration\n";
        return false;
    }

    // Determine device to open
    int open_index = -1;
    std::string open_path;

    if (!config_.device_path.empty()) {
        // Explicit path provided
        open_path = config_.device_path;
    } else if (config_.device_index >= 0) {
        open_index = config_.device_index;
        open_path = "/dev/video" + std::to_string(open_index);
    } else {
        // Auto-detect: find first UVC device
        for (int i = 0; i < 10; ++i) {
            std::string path = "/dev/video" + std::to_string(i);
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;

            struct v4l2_capability cap {};
            bool found = false;
            if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                    (cap.capabilities & V4L2_CAP_STREAMING)) {
                    const char* driver = reinterpret_cast<const char*>(cap.driver);
                    if (std::strstr(driver, "uvcvideo") != nullptr) {
                        found = true;
                        open_index = i;
                        open_path = path;
                    }
                }
            }
            ::close(fd);
            if (found) break;
        }

        if (open_index < 0) {
            std::cerr << "[UsbCamera] No USB webcam detected\n"
                      << "      Check: ls /dev/video*\n"
                      << "      Fix: Connect a USB UVC webcam\n";
            return false;
        }
    }

    // Open with V4L2 backend, fallback to any backend
    bool opened = false;
    if (open_index >= 0) {
        opened = impl_->capture.open(open_index, cv::CAP_V4L2);
        if (!opened) {
            opened = impl_->capture.open(open_index);
        }
    } else {
        opened = impl_->capture.open(open_path, cv::CAP_V4L2);
        if (!opened) {
            opened = impl_->capture.open(open_path);
        }
    }

    if (!opened) {
        std::cerr << "[UsbCamera] Failed to open " << open_path << "\n"
                  << "      Check: ls -la " << open_path << "\n"
                  << "      Fix: Ensure device exists and user has permission (video group)\n";
        return false;
    }

    actual_device_path_ = open_path;

    // Configure resolution and FPS
    impl_->capture.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    impl_->capture.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    impl_->capture.set(cv::CAP_PROP_FPS, config_.fps);

    // Request MJPEG format for better USB bandwidth utilization
    impl_->capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    // V4L2 buffer count hint
    impl_->capture.set(cv::CAP_PROP_BUFFERSIZE, config_.buffer_count);

    // Read back actual values
    const int actual_w = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int actual_h = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const int actual_fps = static_cast<int>(impl_->capture.get(cv::CAP_PROP_FPS));

    std::cout << "[UsbCamera] Opened " << actual_device_path_
              << " (" << actual_w << "x" << actual_h << " @ " << actual_fps << " FPS)\n";

    return true;
}

bool UsbCamera::start() {
    if (!impl_->capture.isOpened()) {
        std::cerr << "[UsbCamera] Cannot start: device not initialized\n";
        return false;
    }

    // Grab a test frame to verify the pipeline works
    if (!impl_->capture.grab()) {
        std::cerr << "[UsbCamera] Failed to grab initial frame from " << actual_device_path_ << "\n";
        return false;
    }

    running_.store(true, std::memory_order_release);
    frame_count_.store(0, std::memory_order_release);

    std::cout << "[UsbCamera] Capture started on " << actual_device_path_ << "\n";
    return true;
}

void UsbCamera::stop() {
    running_.store(false, std::memory_order_release);

    if (impl_ && impl_->capture.isOpened()) {
        impl_->capture.release();
        std::cout << "[UsbCamera] Stopped\n";
    }
}

// ============================================================================
// Frame capture
// ============================================================================

bool UsbCamera::capture_frame(ZeroCopyFrame& frame, int timeout_ms) {
    if (!running_.load(std::memory_order_acquire)) return false;

    (void)timeout_ms;  // V4L2 grab is blocking with internal timeout

    if (!impl_->capture.grab()) return false;
    if (!impl_->capture.retrieve(impl_->current_frame)) return false;

    if (impl_->current_frame.empty()) return false;

    // Populate ZeroCopyFrame descriptor
    const uint64_t seq = frame_count_.fetch_add(1, std::memory_order_relaxed);

    frame = ZeroCopyFrame();  // Zero-init
    frame.sequence = seq;
    frame.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);
    frame.width = impl_->current_frame.cols;
    frame.height = impl_->current_frame.rows;
    frame.format = PixelFormat::BGR888;
    frame.valid = true;

    // Point plane_data at the cv::Mat internal buffer (no copy)
    frame.plane_data[0] = impl_->current_frame.data;
    frame.plane_size[0] = impl_->current_frame.total() * impl_->current_frame.elemSize();
    frame.stride[0] = static_cast<int>(impl_->current_frame.step[0]);

    return true;
}

cv::Mat UsbCamera::wrap_as_mat(const ZeroCopyFrame& frame, PixelFormat /*target_format*/) {
    if (!frame.is_valid() || frame.plane_data[0] == nullptr) {
        return cv::Mat();
    }

    // Wrap the existing buffer as a cv::Mat header (no copy)
    return cv::Mat(frame.height, frame.width, CV_8UC3,
                   frame.plane_data[0], static_cast<size_t>(frame.stride[0]));
}

void UsbCamera::release_frame(ZeroCopyFrame& frame) {
    // No-op: buffer is reused on next capture_frame() call.
    // Clear pointers to prevent dangling access.
    frame.plane_data[0] = nullptr;
    frame.valid = false;
}

}  // namespace aurore
