/**
 * @file dual_camera_manager.cpp
 * @brief Dual-stream camera manager implementation
 */

#include "aurore/dual_camera_manager.hpp"

#include <cstring>
#include <iostream>

namespace aurore {

DualCameraManager::DualCameraManager(const DualCameraConfig& config) : config_(config) {}

DualCameraManager::~DualCameraManager() {
    if (usb_camera_) {
        usb_camera_->stop();
    }
}

bool DualCameraManager::init_mipi(CameraWrapper* mipi_camera) {
    if (!mipi_camera) {
        std::cerr << "[DualCamera] Error: NULL MIPI camera\n";
        return false;
    }

    mipi_camera_ = mipi_camera;
    mipi_active_.store(true, std::memory_order_release);
    mipi_frame_id_.store(0, std::memory_order_release);

    std::cout << "[DualCamera] MIPI camera initialized\n";
    return true;
}

bool DualCameraManager::init_usb(const UsbCameraConfig& usb_config) {
    usb_camera_ = std::make_unique<UsbCamera>(usb_config);

    if (!usb_camera_->init()) {
        std::cerr << "[DualCamera] USB camera init failed\n";
        usb_connected_.store(false, std::memory_order_release);
        return false;
    }

    if (!usb_camera_->start()) {
        std::cerr << "[DualCamera] USB camera start failed\n";
        usb_connected_.store(false, std::memory_order_release);
        return false;
    }

    orb_detector_ = std::make_unique<OrbDetector>();

    usb_active_.store(true, std::memory_order_release);
    usb_connected_.store(true, std::memory_order_release);
    usb_frame_id_.store(0, std::memory_order_release);
    last_usb_process_ns_ = get_timestamp(ClockId::MonotonicRaw);

    std::cout << "[DualCamera] USB camera initialized: " << usb_config.width << "x"
              << usb_config.height << "@" << usb_config.fps << "fps\n";
    return true;
}

DualStreamStatus DualCameraManager::get_status() const {
    DualStreamStatus status;
    status.mipi_active = mipi_active_.load(std::memory_order_acquire);
    status.usb_active = usb_active_.load(std::memory_order_acquire);
    status.mipi_frame_id = mipi_frame_id_.load(std::memory_order_acquire);
    status.usb_frame_id = usb_frame_id_.load(std::memory_order_acquire);
    status.mipi_latency_us = mipi_latency_us_.load(std::memory_order_acquire);
    status.usb_latency_us = usb_latency_us_.load(std::memory_order_acquire);
    status.usb_aligned = usb_aligned_.load(std::memory_order_acquire);
    status.optical_gate_passed = optical_gate_passed_.load(std::memory_order_acquire);
    return status;
}

std::optional<Detection> DualCameraManager::process_usb_frame() {
    if (!usb_camera_ || !usb_connected_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    uint64_t now_ns = get_timestamp(ClockId::MonotonicRaw);

    ZeroCopyFrame usb_frame;
    if (!usb_camera_->capture_frame(usb_frame, config_.usb_frame_timeout_ms)) {
        if (now_ns - last_usb_process_ns_ > kUsbTimeoutNs) {
            std::cerr << "[DualCamera] USB camera timeout - marking disconnected\n";
            on_usb_disconnect();
        }
        return std::nullopt;
    }

    last_usb_process_ns_ = now_ns;

    uint32_t frame_id = usb_frame_id_.fetch_add(1, std::memory_order_acq_rel) + 1;
    usb_frame_ts_ns_.store(now_ns, std::memory_order_release);

    cv::Mat bgr_frame = usb_camera_->wrap_as_mat(usb_frame, PixelFormat::BGR888);
    if (bgr_frame.empty()) {
        return std::nullopt;
    }

    if (usb_frame_cb_) usb_frame_cb_(bgr_frame);

    if (orb_detector_ && orb_detector_->is_ready()) {
        auto detection = orb_detector_->detect(bgr_frame);
        if (detection.has_value()) {
            std::lock_guard<std::mutex> lock(roi_mutex_);
            latest_roi_ = RoiRegion{static_cast<float>(detection->bbox.x),
                                    static_cast<float>(detection->bbox.y),
                                    static_cast<float>(detection->bbox.w),
                                    static_cast<float>(detection->bbox.h),
                                    frame_id,
                                    now_ns};

            std::cout << "[DualCamera] USB detection: ROI(" << latest_roi_->x << ","
                      << latest_roi_->y << "," << latest_roi_->w << "," << latest_roi_->h << ")\n";
        }
        return detection;
    }

    return std::nullopt;
}

std::optional<RoiRegion> DualCameraManager::get_latest_roi() const {
    std::lock_guard<std::mutex> lock(roi_mutex_);
    return latest_roi_;
}

void DualCameraManager::set_usb_aligned(bool aligned) {
    usb_aligned_.store(aligned, std::memory_order_release);
}

void DualCameraManager::set_optical_gate_passed(bool passed) {
    optical_gate_passed_.store(passed, std::memory_order_release);
}

void DualCameraManager::record_mipi_frame(uint64_t timestamp_ns) {
    uint64_t now = get_timestamp(ClockId::MonotonicRaw);
    mipi_frame_id_.fetch_add(1, std::memory_order_acq_rel);
    mipi_frame_ts_ns_.store(timestamp_ns, std::memory_order_release);
    mipi_latency_us_.store((now - timestamp_ns) / 1000, std::memory_order_release);
}

void DualCameraManager::record_usb_frame(uint64_t timestamp_ns) {
    uint64_t now = get_timestamp(ClockId::MonotonicRaw);
    usb_frame_ts_ns_.store(timestamp_ns, std::memory_order_release);
    usb_latency_us_.store((now - timestamp_ns) / 1000, std::memory_order_release);
}

void DualCameraManager::on_usb_disconnect() {
    usb_connected_.store(false, std::memory_order_release);
    usb_active_.store(false, std::memory_order_release);
    optical_gate_passed_.store(false, std::memory_order_release);

    std::cerr << "[DualCamera] WARN: USB camera disconnected - optical gate disabled\n";
    std::cerr << "      Check: ls /dev/video*\n";
    std::cerr << "      Fix: Reconnect USB webcam or system downgrades to SEARCH mode\n";
}

void DualCameraManager::on_usb_reconnect() {
    if (usb_camera_ && usb_camera_->init() && usb_camera_->start()) {
        usb_connected_.store(true, std::memory_order_release);
        usb_active_.store(true, std::memory_order_release);

        std::cout << "[DualCamera] USB camera reconnected\n";
    }
}

}  // namespace aurore
