/**
 * @file camera_wrapper.cpp
 * @brief Camera capture implementation
 *
 * Supports multiple capture modes:
 * 1. Test pattern generator (development, no hardware)
 * 2. OpenCV webcam capture (development with USB webcam)
 * 3. libcamera DMA buffers (production on RPi 5)
 *
 * This file implements modes 1 and 2 for development machine testing.
 */

#include "aurore/camera_wrapper.hpp"

#include <cstring>
#include <iostream>
#include <system_error>

// OpenCV headers
#include <opencv2/opencv.hpp>

namespace aurore {

/**
 * @brief Internal implementation (pimpl pattern)
 *
 * Supports test pattern generation and OpenCV webcam capture.
 */
struct CameraWrapper::Impl {
    int width;
    int height;
    int fps;
    uint64_t frame_counter;
    
    // Capture mode
    bool use_test_pattern;
    bool use_webcam;
    cv::VideoCapture webcam_cap;
    int webcam_id;
    
    // Test pattern state
    cv::Point2f target_pos;
    cv::Point2f target_velocity;
    float target_size;

    bool init_camera() {
        if (use_webcam) {
            // Try to open webcam
            webcam_cap.open(webcam_id, cv::CAP_V4L2);
            if (!webcam_cap.isOpened()) {
                webcam_cap.open(webcam_id);  // Fallback to default backend
            }
            
            if (webcam_cap.isOpened()) {
                webcam_cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
                webcam_cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
                webcam_cap.set(cv::CAP_PROP_FPS, fps);
                std::cout << "Webcam opened: " << width << "x" << height 
                          << " @ " << webcam_cap.get(cv::CAP_PROP_FPS) << " FPS" << std::endl;
                return true;
            } else {
                std::cerr << "Webcam not available, falling back to test pattern" << std::endl;
                use_webcam = false;
                use_test_pattern = true;
            }
        }
        
        if (use_test_pattern) {
            std::cout << "Using test pattern generator (" << width << "x" << height << ")" << std::endl;
            // Initialize target at center with random velocity
            target_pos = cv::Point2f(width / 2.0f, height / 2.0f);
            target_velocity = cv::Point2f(2.0f, 1.5f);  // pixels per frame
            target_size = 30.0f;
            return true;
        }
        
        return true;
    }

    bool configure_stream(const CameraConfig& config) {
        width = config.width;
        height = config.height;
        fps = config.fps;
        frame_counter = 0;
        
        // Check for test pattern mode flag
        use_test_pattern = true;  // Default to test pattern
        use_webcam = false;
        webcam_id = 0;
        
        // Can be overridden by environment variable
        const char* cam_mode = std::getenv("AURORE_CAM_MODE");
        if (cam_mode) {
            std::string mode(cam_mode);
            if (mode == "webcam" || mode == "webcam0") {
                use_webcam = true;
                use_test_pattern = false;
            } else if (mode == "test" || mode == "pattern") {
                use_test_pattern = true;
                use_webcam = false;
            }
        }
        
        return true;
    }

    bool allocate_buffers(const CameraConfig& config) {
        (void)config;
        return true;
    }

    bool create_requests(const CameraConfig& config) {
        (void)config;
        return true;
    }

    void cleanup() {
        if (webcam_cap.isOpened()) {
            webcam_cap.release();
        }
    }

    cv::Mat generate_test_pattern() {
        // Create background (gray)
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar(128, 128, 128));
        
        // Draw grid
        for (int x = 0; x < width; x += 100) {
            cv::line(frame, cv::Point(x, 0), cv::Point(x, height), 
                     cv::Scalar(100, 100, 100), 1);
        }
        for (int y = 0; y < height; y += 100) {
            cv::line(frame, cv::Point(0, y), cv::Point(width, y), 
                     cv::Scalar(100, 100, 100), 1);
        }
        
        // Update target position (bounce off edges)
        target_pos += target_velocity;
        if (target_pos.x < target_size || target_pos.x > width - target_size) {
            target_velocity.x = -target_velocity.x;
        }
        if (target_pos.y < target_size || target_pos.y > height - target_size) {
            target_velocity.y = -target_velocity.y;
        }
        
        // Draw moving target (red circle)
        cv::circle(frame, target_pos, static_cast<int>(target_size), 
                   cv::Scalar(0, 0, 255), -1);
        
        // Draw crosshair at center
        cv::line(frame, cv::Point(width/2 - 20, height/2), cv::Point(width/2 + 20, height/2),
                 cv::Scalar(0, 255, 0), 2);
        cv::line(frame, cv::Point(width/2, height/2 - 20), cv::Point(width/2, height/2 + 20),
                 cv::Scalar(0, 255, 0), 2);
        
        // Draw frame counter
        std::string text = "Frame: " + std::to_string(frame_counter);
        cv::putText(frame, text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 
                    0.7, cv::Scalar(0, 255, 0), 2);
        
        text = "Test Pattern Mode";
        cv::putText(frame, text, cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(255, 255, 0), 2);
        
        return frame;
    }

    bool capture_frame_stub(ZeroCopyFrame& frame) {
        cv::Mat bgr_frame;
        
        if (use_webcam && webcam_cap.isOpened()) {
            // Capture from webcam
            webcam_cap >> bgr_frame;
            if (bgr_frame.empty()) {
                frame.valid = false;
                snprintf(frame.error, sizeof(frame.error), "%s", "Webcam capture failed");
                return false;
            }
        } else {
            // Generate test pattern
            bgr_frame = generate_test_pattern();
        }
        
        // Convert BGR to RAW10 format for zero-copy simulation
        // In production, this would be actual DMA buffer
        // For development, we'll store the BGR data and convert on wrap_as_mat
        
        frame.sequence = frame_counter++;
        frame.timestamp_ns = get_timestamp(ClockId::MonotonicRaw);
        frame.width = width;
        frame.height = height;
        frame.format = PixelFormat::BGR888;  // Store as BGR for easier OpenCV integration
        frame.valid = !bgr_frame.empty();
        
        // Allocate temporary storage for frame data
        // Note: In production, this would be DMA buffer mmap
        uint8_t* frame_data = new uint8_t[width * height * 3];
        std::memcpy(frame_data, bgr_frame.data, width * height * 3);
        
        frame.plane_data[0] = frame_data;
        frame.plane_size[0] = width * height * 3;
        frame.stride[0] = width * 3;
        
        // Set cleanup flag in user data (hack for development)
        frame.error[0] = 1;  // Mark that data[0] needs deletion
        
        snprintf(frame.error + 1, sizeof(frame.error) - 1, "%s", "Development mode - BGR capture");
        
        return frame.validate(width, height);
    }
    
    cv::Mat get_last_frame_as_mat() {
        if (use_webcam && webcam_cap.isOpened()) {
            cv::Mat frame;
            webcam_cap >> frame;
            return frame;
        } else {
            return generate_test_pattern();
        }
    }
};

CameraWrapper::CameraWrapper(const CameraConfig& config)
    : impl_(std::make_unique<Impl>())
    , config_(config)
    , running_(false)
    , frame_count_(0)
    , error_count_(0) {
    
    if (!config_.validate()) {
        throw CameraException("Invalid camera configuration");
    }
}

CameraWrapper::~CameraWrapper() {
    stop();
    impl_->cleanup();
}

bool CameraWrapper::init() {
    try {
        impl_->init_camera();
        impl_->configure_stream(config_);
        impl_->allocate_buffers(config_);
        impl_->create_requests(config_);
        return true;
    }
    catch (const CameraException& e) {
        impl_->cleanup();
        throw;
    }
}

bool CameraWrapper::start() {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }
    
    running_.store(true, std::memory_order_release);
    return true;
}

void CameraWrapper::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    running_.store(false, std::memory_order_release);
}

bool CameraWrapper::capture_frame(ZeroCopyFrame& frame, int timeout_ms) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    
    (void)timeout_ms;  // Unused in stub
    
    return impl_->capture_frame_stub(frame);
}

bool CameraWrapper::try_capture_frame(ZeroCopyFrame& frame) {
    return capture_frame(frame, 0);
}

cv::Mat CameraWrapper::wrap_as_mat(const ZeroCopyFrame& frame,
                                    PixelFormat target_format) {
    // Security: Validate frame before wrapping
    if (!frame.validate(config_.width, config_.height)) {
        std::cerr << "wrap_as_mat: Frame validation failed" << std::endl;
        return cv::Mat();
    }

    if (!frame.is_valid()) {
        return cv::Mat();
    }

    // Development mode: BGR888 frames from test pattern or webcam
    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        // Create a copy since we own the data (will be freed in frame cleanup)
        cv::Mat bgr_img(frame.height, frame.width, CV_8UC3);
        std::memcpy(bgr_img.data, frame.plane_data[0], frame.plane_size[0]);
        return bgr_img;
    }

    // Production mode: RAW10 to BGR888 conversion (Sony IMX708)
    if (frame.format == PixelFormat::RAW10 && target_format == PixelFormat::BGR888) {
        // Create output BGR image
        cv::Mat bgr_img(frame.height, frame.width, CV_8UC3);

        // Get input RAW10 data
        const uint16_t* raw_data = static_cast<const uint16_t*>(frame.plane_data[0]);

        // Find min/max for normalization (simple linear stretch)
        uint16_t min_val = 65535, max_val = 0;
        const size_t pixel_count = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);

        for (size_t i = 0; i < pixel_count; i++) {
            // RAW10 is stored in lower 10 bits of 16-bit word
            const uint16_t pixel = raw_data[i] & 0x03FF;
            if (pixel < min_val) min_val = pixel;
            if (pixel > max_val) max_val = pixel;
        }

        // Convert RAW10 to BGR (grayscale for now - color processing in vision pipeline)
        const float scale = (max_val > min_val) ? 255.0f / static_cast<float>(max_val - min_val) : 1.0f;

        for (size_t i = 0; i < pixel_count; i++) {
            const uint16_t pixel = raw_data[i] & 0x03FF;
            const uint8_t gray = static_cast<uint8_t>((pixel - min_val) * scale);
            bgr_img.at<cv::Vec3b>(static_cast<int>(i / static_cast<size_t>(frame.width)),
                                  static_cast<int>(i % static_cast<size_t>(frame.width))) =
                cv::Vec3b(gray, gray, gray);
        }

        return bgr_img;
    }

    (void)target_format;  // Unused for other formats
    return cv::Mat();
}

bool CameraWrapper::set_exposure(int exposure_us) {
    if (!impl_) {
        return false;
    }
    
    // Stub: Store exposure value
    (void)exposure_us;
    return true;
}

bool CameraWrapper::set_gain(float gain) {
    if (!impl_) {
        return false;
    }
    
    // Stub: Store gain value
    (void)gain;
    return true;
}

// FrameBufferAllocator implementation (stub)

bool FrameBufferAllocator::allocate(int width, int height, PixelFormat format, 
                                     int count) {
    width_ = width;
    height_ = height;
    format_ = format;
    count_ = count;
    
    // Calculate plane sizes based on format
    switch (format) {
        case PixelFormat::RAW10: {
            stride_[0] = width * 2;  // Aligned to 16 bits
            plane_size_[0] = static_cast<size_t>(stride_[0]) * height;
            break;
        }
        
        case PixelFormat::BGR888:
        case PixelFormat::RGB888: {
            stride_[0] = width * 3;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * height;
            break;
        }
        
        case PixelFormat::NV12: {
            stride_[0] = width;  // Y plane
            plane_size_[0] = static_cast<size_t>(stride_[0]) * height;
            stride_[1] = width;  // UV plane (subsampled)
            plane_size_[1] = static_cast<size_t>(stride_[1]) * height / 2;
            break;
        }
        
        case PixelFormat::YUV420: {
            stride_[0] = width;  // Y plane
            plane_size_[0] = static_cast<size_t>(stride_[0]) * height;
            stride_[1] = width / 2;  // U plane
            plane_size_[1] = static_cast<size_t>(stride_[1]) * height / 2;
            stride_[2] = width / 2;  // V plane
            plane_size_[2] = static_cast<size_t>(stride_[2]) * height / 2;
            break;
        }
    }
    
    // Stub: No actual buffer allocation
    buffers_.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        buffers_[i].fd = -1;
        buffers_[i].data = nullptr;
        buffers_[i].size = plane_size_[0];
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
