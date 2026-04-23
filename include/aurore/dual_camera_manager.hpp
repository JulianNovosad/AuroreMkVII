/**
 * @file dual_camera_manager.hpp
 * @brief Dual-stream camera manager for Aurore MkVII
 *
 * Implements parallel MIPI CSI-2 (primary) and USB Webcam (secondary) streams.
 * Zero-copy invariant: no memcpy between V4L2 buffers and OpenCV headers.
 *
 * Hardware:
 *   Primary:   MIPI CSI-2 (Dev 0, 120Hz, RAW10) - High-speed tracking
 *   Secondary: USB Webcam (Dev 1, 30-60Hz, BGR) - Wide-area detection
 *
 * Workflow:
 *   1. USB stream triggers ORBDetector to find potential targets
 *   2. Upon detection, ROI coordinates passed to MIPI KcfTracker
 *   3. Optical Logic Gate validates alignment between streams
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <chrono>

#include "aurore/camera_wrapper.hpp"
#include "aurore/usb_camera.hpp"
#include "aurore/detector.hpp"
#include "aurore/tracker.hpp"
#include "aurore/ring_buffer.hpp"
#include "aurore/timing.hpp"

namespace aurore {

struct DualCameraConfig {
    int mipi_width = 1536;
    int mipi_height = 864;
    int mipi_fps = 120;
    int usb_width = 640;
    int usb_height = 480;
    int usb_fps = 30;
    
    float alignment_variance_px = 50.0f;
    int usb_frame_timeout_ms = 100;
    int mipi_frame_timeout_ms = 10;
};

struct DualStreamStatus {
    bool mipi_active = false;
    bool usb_active = false;
    uint32_t mipi_frame_id = 0;
    uint32_t usb_frame_id = 0;
    uint64_t mipi_latency_us = 0;
    uint64_t usb_latency_us = 0;
    bool usb_aligned = false;
    bool optical_gate_passed = false;
};

struct RoiRegion {
    float x, y, w, h;
    uint32_t source_frame_id;
    uint64_t timestamp_ns;
};

class DualCameraManager {
public:
    explicit DualCameraManager(const DualCameraConfig& config = DualCameraConfig());
    ~DualCameraManager();

    bool init_mipi(CameraWrapper* mipi_camera);
    bool init_usb(const UsbCameraConfig& usb_config);
    
    bool is_mipi_active() const { return mipi_active_.load(std::memory_order_acquire); }
    bool is_usb_active() const { return usb_active_.load(std::memory_order_acquire); }
    bool is_usb_connected() const { return usb_connected_; }
    
    DualStreamStatus get_status() const;
    
    std::optional<Detection> process_usb_frame();

    // Optional callback invoked with the raw BGR frame on every successful USB capture.
    // Called from whichever thread calls process_usb_frame().
    using UsbFrameCallback = std::function<void(const cv::Mat&)>;
    void set_usb_frame_callback(UsbFrameCallback cb) { usb_frame_cb_ = std::move(cb); }
    
    std::optional<RoiRegion> get_latest_roi() const;
    
    void set_usb_aligned(bool aligned);
    void set_optical_gate_passed(bool passed);
    
    void record_mipi_frame(uint64_t timestamp_ns);
    void record_usb_frame(uint64_t timestamp_ns);
    
    void on_usb_disconnect();
    void on_usb_reconnect();

private:
    UsbFrameCallback usb_frame_cb_;

    DualCameraConfig config_;
    
    CameraWrapper* mipi_camera_ = nullptr;
    std::unique_ptr<UsbCamera> usb_camera_;
    std::unique_ptr<OrbDetector> orb_detector_;
    
    std::atomic<bool> mipi_active_{false};
    std::atomic<bool> usb_active_{false};
    std::atomic<bool> usb_connected_{false};
    std::atomic<bool> optical_gate_passed_{false};
    
    std::atomic<uint32_t> mipi_frame_id_{0};
    std::atomic<uint32_t> usb_frame_id_{0};
    
    std::atomic<uint64_t> mipi_frame_ts_ns_{0};
    std::atomic<uint64_t> usb_frame_ts_ns_{0};
    
    std::atomic<uint64_t> mipi_latency_us_{0};
    std::atomic<uint64_t> usb_latency_us_{0};
    
    std::atomic<bool> usb_aligned_{false};
    
    std::optional<RoiRegion> latest_roi_;
    mutable std::mutex roi_mutex_;
    
    uint64_t last_usb_process_ns_ = 0;
    static constexpr uint64_t kUsbTimeoutNs = 100000000;
};

}  // namespace aurore
