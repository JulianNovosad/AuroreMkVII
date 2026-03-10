/**
 * @file mock_camera_wrapper.cpp
 * @brief Mock camera implementation for laptop development (no libcamera required)
 *
 * Generates synthetic test pattern frames with a moving target for
 * testing the vision pipeline without hardware.
 */

#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <thread>
#include <vector>

#include "aurore/camera_auth.hpp"
#include "aurore/camera_wrapper.hpp"

namespace aurore {

/**
 * @brief Internal implementation for mock camera
 */
struct MockCameraImpl {
    int width;
    int height;
    int fps;
    std::atomic<uint64_t> frame_counter{0};
    std::atomic<bool> running{false};
    std::thread capture_thread;

    // Test pattern state
    cv::Point2f target_pos;
    cv::Point2f target_velocity;
    float target_size;
    std::mutex target_mutex;

    // Frame buffer (circular buffer for latest frame)
    cv::Mat latest_frame;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;

    bool configure(const CameraConfig& config) {
        width = config.width;
        height = config.height;
        fps = config.fps;
        frame_counter = 0;

        // Initialize target at center with velocity
        target_pos =
            cv::Point2f(static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f);
        target_velocity = cv::Point2f(3.0f, 2.0f);  // pixels per frame
        target_size = 40.0f;

        // Generate initial frame so it's ready immediately
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            generate_frame(latest_frame, frame_counter.fetch_add(1, std::memory_order_relaxed));
        }

        std::cout << "MockCamera: configured " << width << "x" << height << " @ " << fps
                  << " FPS (test pattern mode)" << std::endl;
        return true;
    }

    void generate_frame(cv::Mat& frame, uint64_t frame_id) {
        std::lock_guard<std::mutex> lock(target_mutex);

        // Create background (medium gray)
        frame = cv::Mat(height, width, CV_8UC3, cv::Scalar(128, 128, 128));

        // Draw grid pattern
        for (int x = 0; x < width; x += 100) {
            cv::line(frame, cv::Point(x, 0), cv::Point(x, height), cv::Scalar(100, 100, 100), 1);
        }
        for (int y = 0; y < height; y += 100) {
            cv::line(frame, cv::Point(0, y), cv::Point(width, y), cv::Scalar(100, 100, 100), 1);
        }

        // Update target position (bounce off edges)
        target_pos += target_velocity;
        if (target_pos.x < target_size || target_pos.x > static_cast<float>(width) - target_size) {
            target_velocity.x = -target_velocity.x;
        }
        if (target_pos.y < target_size || target_pos.y > static_cast<float>(height) - target_size) {
            target_velocity.y = -target_velocity.y;
        }

        // Draw moving target (red rectangle - simulates primary kinetic target)
        cv::Rect target_rect(static_cast<int>(target_pos.x - target_size),
                             static_cast<int>(target_pos.y - target_size),
                             static_cast<int>(target_size * 2), static_cast<int>(target_size * 2));
        cv::rectangle(frame, target_rect, cv::Scalar(0, 0, 200), -1);

        // Add some texture to target (cross pattern)
        cv::line(frame,
                 cv::Point(static_cast<int>(target_pos.x - target_size * 0.5f),
                           static_cast<int>(target_pos.y)),
                 cv::Point(static_cast<int>(target_pos.x + target_size * 0.5f),
                           static_cast<int>(target_pos.y)),
                 cv::Scalar(200, 200, 0), 2);
        cv::line(frame,
                 cv::Point(static_cast<int>(target_pos.x),
                           static_cast<int>(target_pos.y - target_size * 0.5f)),
                 cv::Point(static_cast<int>(target_pos.x),
                           static_cast<int>(target_pos.y + target_size * 0.5f)),
                 cv::Scalar(200, 200, 0), 2);

        // Draw crosshair at center (reticle)
        cv::line(frame, cv::Point(width / 2 - 20, height / 2),
                 cv::Point(width / 2 + 20, height / 2), cv::Scalar(0, 255, 0), 2);
        cv::line(frame, cv::Point(width / 2, height / 2 - 20),
                 cv::Point(width / 2, height / 2 + 20), cv::Scalar(0, 255, 0), 2);

        // Draw frame counter
        std::string text = "Frame: " + std::to_string(frame_id);
        cv::putText(frame, text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 255, 0), 2);

        text = "Mock Camera - Laptop Mode";
        cv::putText(frame, text, cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(255, 255, 0), 2);

        // Add target position indicator
        text = "Target: (" + std::to_string(static_cast<int>(target_pos.x)) + ", " +
               std::to_string(static_cast<int>(target_pos.y)) + ")";
        cv::putText(frame, text, cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(255, 200, 0), 2);
    }

    void capture_loop() {
        const uint32_t frame_period_us = 1000000 / static_cast<uint32_t>(fps);

        while (running.load(std::memory_order_acquire)) {
            auto start = std::chrono::steady_clock::now();

            // Generate frame
            uint64_t frame_id = frame_counter.fetch_add(1, std::memory_order_relaxed);
            cv::Mat frame;
            generate_frame(frame, frame_id);

            // Store as latest frame
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                latest_frame = frame.clone();
            }
            frame_cv.notify_all();

            // Timing control
            auto end = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            if (elapsed < static_cast<int64_t>(frame_period_us)) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<int64_t>(frame_period_us) - elapsed));
            }
        }
    }
};

// ============================================================================
// CameraWrapper Implementation
// ============================================================================

struct CameraWrapper::Impl {
    MockCameraImpl mock;
};

CameraWrapper::CameraWrapper(const CameraConfig& config)
    : impl_(std::make_unique<Impl>()),
      config_(config),
      running_(false),
      frame_count_(0),
      error_count_(0) {
    if (!config_.validate()) {
        throw CameraException("Invalid camera configuration");
    }
}

CameraWrapper::~CameraWrapper() { stop(); }

bool CameraWrapper::init() { return impl_->mock.configure(config_); }

bool CameraWrapper::start() {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }

    running_.store(true, std::memory_order_release);
    impl_->mock.running.store(true, std::memory_order_release);

    // Start capture thread
    impl_->mock.capture_thread = std::thread(&MockCameraImpl::capture_loop, &impl_->mock);

    std::cout << "MockCamera started" << std::endl;
    return true;
}

void CameraWrapper::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);
    impl_->mock.running.store(false, std::memory_order_release);

    // Wait for capture thread
    if (impl_->mock.capture_thread.joinable()) {
        impl_->mock.capture_thread.join();
    }

    std::cout << "MockCamera stopped" << std::endl;
}

bool CameraWrapper::capture_frame(ZeroCopyFrame& frame, int /*timeout_ms*/) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    // Get latest frame
    cv::Mat bgr_frame;
    {
        std::lock_guard<std::mutex> lock(impl_->mock.frame_mutex);
        if (impl_->mock.latest_frame.empty()) {
            frame.valid = false;
            return false;
        }
        bgr_frame = impl_->mock.latest_frame.clone();
    }

    // Allocate frame data (caller must free via frame cleanup)
    size_t data_size = static_cast<size_t>(bgr_frame.cols * bgr_frame.rows * 3);
    // 64-byte aligned allocation for SIMD optimization
    uint8_t* frame_data = static_cast<uint8_t*>(aligned_alloc(64, data_size));
    if (!frame_data) {
        frame.valid = false;
        std::snprintf(frame.error, sizeof(frame.error), "%s", "aligned_alloc failed");
        return false;
    }
    std::memcpy(frame_data, bgr_frame.data, data_size);

    // Runtime alignment check (debug assertion)
    if ((reinterpret_cast<uintptr_t>(frame_data) & 0x3Fu) != 0) {
        std::fprintf(stderr, "FATAL: mock frame data not 64-byte aligned at %p\n", frame_data);
        std::abort();
    }

    // Fill ZeroCopyFrame
    frame.sequence = impl_->mock.frame_counter.load(std::memory_order_relaxed);
    frame.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);
    frame.width = static_cast<int>(bgr_frame.cols);
    frame.height = static_cast<int>(bgr_frame.rows);
    frame.format = PixelFormat::BGR888;
    frame.valid = true;

    frame.plane_data[0] = frame_data;
    frame.plane_size[0] = data_size;
    frame.stride[0] = bgr_frame.cols * 3;

    // Mark for cleanup (hack: use error field to signal data needs deletion)
    frame.error[0] = 1;
    std::snprintf(frame.error + 1, sizeof(frame.error) - 1, "%s", "Mock frame - free() on cleanup");

    // Compute frame authentication (SHA256 + HMAC) - ICD-001 / AM7-L2-SEC-001
    authenticate_frame(frame);

    return frame.validate(config_.width, config_.height);
}

bool CameraWrapper::try_capture_frame(ZeroCopyFrame& frame) { return capture_frame(frame, 0); }

cv::Mat CameraWrapper::wrap_as_mat(const ZeroCopyFrame& frame, PixelFormat target_format) {
    if (!frame.is_valid()) {
        return cv::Mat();
    }

    // BGR888 direct copy
    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        cv::Mat bgr_img(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3);
        std::memcpy(bgr_img.data, frame.plane_data[0], frame.plane_size[0]);
        return bgr_img;
    }

    return cv::Mat();
}

bool CameraWrapper::set_exposure(int exposure_us) {
    (void)exposure_us;
    return true;  // Mock - no-op
}

bool CameraWrapper::set_gain(float gain) {
    (void)gain;
    return true;  // Mock - no-op
}

void CameraWrapper::release_frame(ZeroCopyFrame& frame) {
    // Mock implementation: cleanup frame data if marked for deletion
    if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
        // Free aligned memory (allocated with aligned_alloc)
        free(frame.plane_data[0]);
        frame.plane_data[0] = nullptr;
        frame.valid = false;
        frame.error[0] = 0;
    }
}

// ============================================================================
// FrameBufferAllocator Implementation (stub for laptop)
// ============================================================================

bool FrameBufferAllocator::allocate(int width, int height, PixelFormat format, int count) {
    width_ = width;
    height_ = height;
    format_ = format;
    count_ = count;

    size_t w = static_cast<size_t>(width);
    size_t h = static_cast<size_t>(height);

    // Calculate plane sizes
    switch (format) {
        case PixelFormat::RAW10:
            stride_[0] = width * 2;
            plane_size_[0] = w * 2 * h;
            break;
        case PixelFormat::BGR888:
        case PixelFormat::RGB888:
            stride_[0] = width * 3;
            plane_size_[0] = w * 3 * h;
            break;
        case PixelFormat::NV12:
            stride_[0] = width;
            plane_size_[0] = w * h;
            stride_[1] = width;
            plane_size_[1] = w * h / 2;
            break;
        case PixelFormat::YUV420:
            stride_[0] = width;
            plane_size_[0] = w * h;
            stride_[1] = width / 2;
            plane_size_[1] = (w / 2) * (h / 2);
            stride_[2] = width / 2;
            plane_size_[2] = (w / 2) * (h / 2);
            break;
    }

    // Mock: no actual buffer allocation
    buffers_.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        size_t idx = static_cast<size_t>(i);
        buffers_[idx].fd = -1;
        buffers_[idx].data = nullptr;
        buffers_[idx].size = plane_size_[0];
    }

    return true;
}

void FrameBufferAllocator::free() {
    for (auto& buffer : buffers_) {
        if (buffer.data) {
            munmap(buffer.data, buffer.size);
        }
        if (buffer.fd >= 0) {
            close(buffer.fd);
        }
    }
    buffers_.clear();
}

void* FrameBufferAllocator::get_data(int index, int /*plane*/) {
    if (index < 0 || index >= count_) {
        return nullptr;
    }
    return buffers_[static_cast<size_t>(index)].data;
}

}  // namespace aurore
