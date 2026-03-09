/**
 * @file camera_wrapper.cpp
 * @brief Camera capture implementation
 *
 * Supports three capture modes selected by the AURORE_CAM_MODE env var:
 *   (unset)   → libcamera (real IMX708 on RPi 5), fallback to test pattern
 *   "test"    → test pattern generator (development, no hardware)
 *   "webcam"  → OpenCV webcam capture (USB webcam)
 *
 * On AURORE_LAPTOP_BUILD (test / unit-test builds), libcamera is absent;
 * only the test-pattern and webcam paths are compiled.
 *
 * The real libcamera path:
 *   1. init_libcamera()    — configure stream, allocate DMA buffers, queue requests
 *   2. capture_libcamera() — wait on requestCompleted, copy data, requeue
 *   3. cleanup_libcamera() — stop camera, unmap DMA buffers, release resources
 */

#include "aurore/camera_wrapper.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <system_error>

#include <sys/mman.h>

// OpenCV headers
#include <opencv2/opencv.hpp>

// ARM NEON headers for SIMD optimization
#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#define AURORE_HAS_NEON
#endif

// libcamera headers — only available on non-laptop (hardware) builds
#ifndef AURORE_LAPTOP_BUILD
#include <libcamera/libcamera.h>
#endif

// Security headers for frame authentication
#include "aurore/security.hpp"

namespace aurore {

/**
 * @brief Internal implementation (pimpl pattern)
 *
 * Supports libcamera (real hardware), test pattern, and OpenCV webcam.
 * The libcamera fields and methods are conditionally compiled.
 */
struct CameraWrapper::Impl {
    int width  = 0;
    int height = 0;
    int fps    = 0;
    uint64_t frame_counter = 0;

    // --- Capture mode flags (set by configure_stream) ---
    bool use_libcamera    = false;
    bool use_test_pattern = false;
    bool use_webcam       = false;

    // --- OpenCV webcam state ---
    cv::VideoCapture webcam_cap;
    int webcam_id = 0;

    // --- Test pattern state ---
    cv::Point2f target_pos;
    cv::Point2f target_velocity;
    float target_size = 30.0f;

#ifndef AURORE_LAPTOP_BUILD
    // =========================================================================
    // libcamera state (hardware builds only)
    // =========================================================================
    std::unique_ptr<libcamera::CameraManager>        lc_cm;
    std::shared_ptr<libcamera::Camera>               lc_camera;
    std::unique_ptr<libcamera::FrameBufferAllocator> lc_allocator;
    libcamera::Stream*                               lc_stream = nullptr;
    std::vector<std::unique_ptr<libcamera::Request>> lc_requests;

    // DMA buffer mapping: FrameBuffer* → {mmap ptr, size}
    struct MappedBuf { void* data; size_t size; };
    std::unordered_map<const libcamera::FrameBuffer*, MappedBuf> lc_mapped;

    // Completed-request synchronisation
    std::mutex              lc_mutex;
    std::condition_variable lc_cv;
    std::queue<libcamera::Request*> lc_completed;
    bool lc_stopped = false;
#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Mode selection
    // =========================================================================

    bool configure_stream(const CameraConfig& config) {
        width         = config.width;
        height        = config.height;
        fps           = config.fps;
        frame_counter = 0;

        use_libcamera    = false;
        use_test_pattern = false;
        use_webcam       = false;
        webcam_id        = 0;

        const char* cam_mode = std::getenv("AURORE_CAM_MODE");
        if (cam_mode) {
            const std::string mode(cam_mode);
            if (mode == "webcam" || mode == "webcam0") {
                use_webcam = true;
            } else if (mode == "test" || mode == "pattern") {
                use_test_pattern = true;
            } else if (mode == "libcamera" || mode == "camera") {
                use_libcamera = true;
            } else {
                use_test_pattern = true;  // Unknown → safe fallback
            }
        } else {
#ifndef AURORE_LAPTOP_BUILD
            // Default on hardware builds: try libcamera, fallback on failure
            use_libcamera = true;
#else
            use_test_pattern = true;
#endif
        }
        return true;
    }

    // =========================================================================
    // Init / cleanup dispatch
    // =========================================================================

    bool init_camera(const CameraConfig& config) {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            if (init_libcamera(config)) {
                return true;
            }
            std::cerr << "[camera] libcamera init failed — falling back to test pattern\n";
            use_libcamera    = false;
            use_test_pattern = true;
        }
#else
        (void)config;
#endif

        if (use_webcam) {
            webcam_cap.open(webcam_id, cv::CAP_V4L2);
            if (!webcam_cap.isOpened()) {
                webcam_cap.open(webcam_id);
            }
            if (webcam_cap.isOpened()) {
                webcam_cap.set(cv::CAP_PROP_FRAME_WIDTH,  width);
                webcam_cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
                webcam_cap.set(cv::CAP_PROP_FPS,          fps);
                std::cout << "[camera] Webcam opened: " << width << "x" << height
                          << " @ " << webcam_cap.get(cv::CAP_PROP_FPS) << " FPS\n";
                return true;
            }
            std::cerr << "[camera] Webcam unavailable — falling back to test pattern\n";
            use_webcam       = false;
            use_test_pattern = true;
        }

        if (use_test_pattern) {
            std::cout << "[camera] Test pattern generator ("
                      << width << "x" << height << ")\n";
            target_pos      = cv::Point2f(static_cast<float>(width)  / 2.0f,
                                          static_cast<float>(height) / 2.0f);
            target_velocity = cv::Point2f(2.0f, 1.5f);
            target_size     = 30.0f;
        }
        return true;
    }

    void cleanup() {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            cleanup_libcamera();
        }
#endif
        if (webcam_cap.isOpened()) {
            webcam_cap.release();
        }
    }

#ifndef AURORE_LAPTOP_BUILD
    // =========================================================================
    // libcamera implementation (hardware builds only)
    // =========================================================================

    /**
     * @brief Initialise libcamera: configure stream, allocate DMA buffers, queue requests.
     */
    bool init_libcamera(const CameraConfig& config) {
        lc_cm = std::make_unique<libcamera::CameraManager>();
        if (lc_cm->start() != 0) {
            std::cerr << "[camera] CameraManager start failed\n";
            lc_cm.reset();
            return false;
        }

        if (lc_cm->cameras().empty()) {
            std::cerr << "[camera] No cameras found\n";
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        lc_camera = lc_cm->cameras()[0];
        if (lc_camera->acquire() != 0) {
            std::cerr << "[camera] Camera acquire failed\n";
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        auto cfg = lc_camera->generateConfiguration({libcamera::StreamRole::Raw});
        if (!cfg) {
            std::cerr << "[camera] generateConfiguration failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        auto& scfg        = cfg->at(0);
        scfg.size.width   = static_cast<unsigned int>(config.width);
        scfg.size.height  = static_cast<unsigned int>(config.height);
        scfg.pixelFormat  = libcamera::formats::SGRBG10_CSI2P;
        scfg.bufferCount  = static_cast<unsigned int>(config.buffer_count);

        if (cfg->validate() == libcamera::CameraConfiguration::Invalid) {
            std::cerr << "[camera] Configuration invalid\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        if (lc_camera->configure(cfg.get()) != 0) {
            std::cerr << "[camera] configure() failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        lc_stream = scfg.stream();

        lc_allocator = std::make_unique<libcamera::FrameBufferAllocator>(lc_camera);
        if (lc_allocator->allocate(lc_stream) < 0) {
            std::cerr << "[camera] Buffer allocation failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        for (const auto& fb : lc_allocator->buffers(lc_stream)) {
            const auto& plane = fb->planes()[0];
            const off_t off   = (plane.offset != libcamera::FrameBuffer::Plane::kInvalidOffset)
                                ? static_cast<off_t>(plane.offset) : 0;
            void* mapped = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                                plane.fd.get(), off);
            if (mapped == MAP_FAILED) {
                std::cerr << "[camera] mmap failed: " << strerror(errno) << "\n";
                continue;
            }
            lc_mapped[fb.get()] = {mapped, plane.length};

            auto req = lc_camera->createRequest();
            if (req && req->addBuffer(lc_stream, fb.get()) == 0) {
                lc_requests.push_back(std::move(req));
            }
        }

        if (lc_requests.empty()) {
            std::cerr << "[camera] No capture requests created\n";
            cleanup_libcamera();
            return false;
        }

        lc_camera->requestCompleted.connect(this, &Impl::on_request_completed);

        libcamera::ControlList controls(lc_camera->controls());
        controls.set(libcamera::controls::ExposureTime,
                     static_cast<int32_t>(config.exposure_us));
        controls.set(libcamera::controls::AnalogueGain, config.gain);

        if (lc_camera->start(&controls) != 0) {
            std::cerr << "[camera] camera->start() failed\n";
            cleanup_libcamera();
            return false;
        }

        for (auto& req : lc_requests) {
            lc_camera->queueRequest(req.get());
        }

        std::cout << "[camera] libcamera: "
                  << scfg.size.width << "x" << scfg.size.height
                  << " " << scfg.pixelFormat.toString()
                  << " stride=" << scfg.stride << "\n";
        return true;
    }

    /** @brief Signal slot — called from CameraManager's event thread. */
    void on_request_completed(libcamera::Request* req) {
        if (req->status() == libcamera::Request::RequestCancelled) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(lc_mutex);
            lc_completed.push(req);
        }
        lc_cv.notify_one();
    }

    /**
     * @brief Blocking libcamera frame capture (zero-copy).
     *
     * Returns a descriptor pointing directly to the DMA buffer.
     * Consumer MUST call release_frame() when done.
     */
    bool capture_libcamera(ZeroCopyFrame& frame, int timeout_ms) {
        std::unique_lock<std::mutex> lock(lc_mutex);
        const bool got = lc_cv.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !lc_completed.empty() || lc_stopped; });

        if (!got || lc_completed.empty()) {
            frame.valid = false;
            snprintf(frame.error, sizeof(frame.error), "%s", "libcamera timeout");
            return false;
        }

        libcamera::Request* req = lc_completed.front();
        lc_completed.pop();
        lock.unlock();

        const auto& bufs = req->buffers();
        const auto  it   = bufs.find(lc_stream);
        if (it == bufs.end()) {
            req->reuse(libcamera::Request::ReuseBuffers);
            lc_camera->queueRequest(req);
            return false;
        }

        libcamera::FrameBuffer* fb = it->second;
        const auto& meta           = fb->metadata();

        const auto mit = lc_mapped.find(fb);
        if (mit == lc_mapped.end()) {
            req->reuse(libcamera::Request::ReuseBuffers);
            lc_camera->queueRequest(req);
            return false;
        }

        frame.sequence      = meta.sequence;
        frame.timestamp_ns  = static_cast<TimestampNs>(meta.timestamp);
        frame.width         = width;
        frame.height        = height;
        frame.format        = PixelFormat::RAW10;
        frame.plane_data[0] = mit->second.data;
        frame.plane_size[0] = mit->second.size;
        // SGRBG10_CSI2P: packed RAW10 stride = ceil(width * 10 / 8)
        frame.stride[0]     = static_cast<int>((static_cast<unsigned int>(width) * 10u + 7u) / 8u);

        frame.request_ptr   = req;
        frame.valid         = true;
        frame.error[0]      = 0;  // DMA buffer

        // Compute frame authentication (SHA256 + HMAC) - ICD-001 / AM7-L2-SEC-001
        // Note: This is synchronous for correctness; async version available via AsyncFrameAuthenticator
        authenticate_frame(frame);

        frame_counter++;
        return frame.validate(width, height);
    }

#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Common methods (all builds)
    // =========================================================================

    void release_frame(ZeroCopyFrame& frame) {
        if (!frame.valid) return;

#ifndef AURORE_LAPTOP_BUILD
        if (frame.request_ptr && use_libcamera) {
            libcamera::Request* req = static_cast<libcamera::Request*>(frame.request_ptr);
            req->reuse(libcamera::Request::ReuseBuffers);
            lc_camera->queueRequest(req);
            frame.request_ptr = nullptr;
            return;
        }
#endif

        // Free aligned memory (allocated with aligned_alloc)
        if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
            free(frame.plane_data[0]);
            frame.plane_data[0] = nullptr;
            frame.error[0] = 0;
        }
    }

#ifndef AURORE_LAPTOP_BUILD
    /** @brief Stop and release all libcamera resources. */
    void cleanup_libcamera() {
        {
            std::lock_guard<std::mutex> lock(lc_mutex);
            lc_stopped = true;
        }
        lc_cv.notify_all();

        if (lc_camera) {
            lc_camera->stop();
            lc_requests.clear();

            for (auto& [fb, buf] : lc_mapped) {
                munmap(buf.data, buf.size);
            }
            lc_mapped.clear();

            if (lc_allocator && lc_stream) {
                lc_allocator->free(lc_stream);
            }
            lc_allocator.reset();
            lc_stream = nullptr;

            lc_camera->release();
            lc_camera.reset();
        }

        if (lc_cm) {
            lc_cm->stop();
            lc_cm.reset();
        }
    }
#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Test pattern
    // =========================================================================

    cv::Mat generate_test_pattern() {
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar(128, 128, 128));

        for (int x = 0; x < width; x += 100) {
            cv::line(frame, cv::Point(x, 0), cv::Point(x, height),
                     cv::Scalar(100, 100, 100), 1);
        }
        for (int y = 0; y < height; y += 100) {
            cv::line(frame, cv::Point(0, y), cv::Point(width, y),
                     cv::Scalar(100, 100, 100), 1);
        }

        target_pos += target_velocity;
        if (target_pos.x < target_size ||
            target_pos.x > static_cast<float>(width) - target_size) {
            target_velocity.x = -target_velocity.x;
        }
        if (target_pos.y < target_size ||
            target_pos.y > static_cast<float>(height) - target_size) {
            target_velocity.y = -target_velocity.y;
        }

        cv::circle(frame, target_pos, static_cast<int>(target_size),
                   cv::Scalar(0, 0, 255), -1);

        const int cx = width / 2, cy = height / 2;
        cv::line(frame, cv::Point(cx - 20, cy), cv::Point(cx + 20, cy),
                 cv::Scalar(0, 255, 0), 2);
        cv::line(frame, cv::Point(cx, cy - 20), cv::Point(cx, cy + 20),
                 cv::Scalar(0, 255, 0), 2);

        cv::putText(frame, "Frame: " + std::to_string(frame_counter),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "Test Pattern Mode",
                    cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(255, 255, 0), 2);

        return frame;
    }

    // =========================================================================
    // Unified capture dispatch
    // =========================================================================

    bool capture_frame_stub(ZeroCopyFrame& frame, int timeout_ms = 100) {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            return capture_libcamera(frame, timeout_ms);
        }
#else
        (void)timeout_ms;
#endif

        cv::Mat bgr_frame;
        if (use_webcam && webcam_cap.isOpened()) {
            webcam_cap >> bgr_frame;
            if (bgr_frame.empty()) {
                frame.valid = false;
                snprintf(frame.error, sizeof(frame.error),
                         "%s", "Webcam capture failed");
                return false;
            }
        } else {
            bgr_frame = generate_test_pattern();
        }

        frame.sequence      = frame_counter++;
        frame.timestamp_ns  = get_timestamp(ClockId::MonotonicRaw);
        frame.width         = width;
        frame.height        = height;
        frame.format        = PixelFormat::BGR888;
        frame.valid         = !bgr_frame.empty();

        const size_t sz = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
        // 64-byte aligned allocation for SIMD optimization
        auto* frame_data = static_cast<uint8_t*>(aligned_alloc(64, sz));
        if (!frame_data) {
            frame.valid = false;
            snprintf(frame.error, sizeof(frame.error), "%s", "aligned_alloc failed");
            return false;
        }
        std::memcpy(frame_data, bgr_frame.data, sz);

        // Runtime alignment check (debug assertion)
        if ((reinterpret_cast<uintptr_t>(frame_data) & 0x3Fu) != 0) {
            std::fprintf(stderr, "FATAL: frame data not 64-byte aligned at %p\n", frame_data);
            std::abort();
        }

        frame.plane_data[0] = frame_data;
        frame.plane_size[0] = sz;
        frame.stride[0]     = width * 3;
        frame.error[0]      = 1;  // Mark heap-allocated

        snprintf(frame.error + 1, sizeof(frame.error) - 1,
                 "%s", "Development mode - BGR capture");

        // Compute frame authentication (SHA256 + HMAC) - ICD-001 / AM7-L2-SEC-001
        authenticate_frame(frame);

        return frame.validate(width, height);
    }
};

// =============================================================================
// CameraWrapper public interface
// =============================================================================

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
        // configure_stream must run before init_camera to set mode flags
        impl_->configure_stream(config_);
        if (!impl_->init_camera(config_)) {
            impl_->cleanup();
            throw CameraException("Camera initialization failed");
        }
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

#ifndef AURORE_LAPTOP_BUILD
    // Signal libcamera shutdown so capture_libcamera() unblocks
    if (impl_ && impl_->use_libcamera && impl_->lc_camera) {
        {
            std::lock_guard<std::mutex> lock(impl_->lc_mutex);
            impl_->lc_stopped = true;
        }
        impl_->lc_cv.notify_all();
        impl_->lc_camera->stop();
    }
#endif
}

bool CameraWrapper::capture_frame(ZeroCopyFrame& frame, int timeout_ms) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    return impl_->capture_frame_stub(frame, timeout_ms);
}

bool CameraWrapper::try_capture_frame(ZeroCopyFrame& frame) {
    return capture_frame(frame, 0);
}

void CameraWrapper::release_frame(ZeroCopyFrame& frame) {
    if (impl_) {
        impl_->release_frame(frame);
    }
}

cv::Mat CameraWrapper::wrap_as_mat(const ZeroCopyFrame& frame,
                                    PixelFormat target_format) {
    if (!frame.validate(config_.width, config_.height)) {
        std::cerr << "wrap_as_mat: Frame validation failed\n";
        return cv::Mat();
    }
    if (!frame.is_valid()) {
        return cv::Mat();
    }

    // Development/webcam: BGR888 frame stored as heap copy
    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        cv::Mat bgr_img(frame.height, frame.width, CV_8UC3);
        std::memcpy(bgr_img.data, frame.plane_data[0], frame.plane_size[0]);
        return bgr_img;
    }

    // Hardware: SGRBG10_CSI2P packed RAW10 → greyscale BGR888
    // Note: stride = ceil(width*10/8); 4 pixels packed into 5 bytes.
    // Full colour Bayer demosaicing is a TODO for the vision pipeline.
    if (frame.format == PixelFormat::RAW10 && target_format == PixelFormat::BGR888) {
        cv::Mat bgr_img(frame.height, frame.width, CV_8UC3);
        const uint8_t* raw = static_cast<const uint8_t*>(frame.plane_data[0]);
        const int stride   = frame.stride[0];

#ifdef AURORE_HAS_NEON
        // NEON optimized 10-bit to 8-bit greyscale conversion
        // Processes 32 pixels per iteration (40 bytes of RAW10)
        for (int row = 0; row < frame.height; ++row) {
            const uint8_t* line = raw + row * stride;
            uint8_t* out = bgr_img.ptr<uint8_t>(row);
            
            int col = 0;
            // Process 32 pixels (40 bytes of RAW10) at a time using vld5
            for (; col <= frame.width - 32; col += 32) {
                // vld5_u8 pulls 5 * 8 = 40 bytes.
                // 40 bytes of RAW10 = 32 pixels.
                // v.val[0..3] each contain 8 pixels (high 8 bits).
                uint8x8x5_t v = vld5_u8(line);
                line += 40;
                
                for (int i = 0; i < 4; ++i) {
                    uint8x8x3_t bgr;
                    bgr.val[0] = v.val[i]; // B
                    bgr.val[1] = v.val[i]; // G
                    bgr.val[2] = v.val[i]; // R
                    vst3_u8(out, bgr);
                    out += 24; // 8 pixels * 3 bytes
                }
            }
            
            // Revert to software for remaining pixels or if width not multiple of 32
            for (; col < frame.width; col += 4) {
                const uint16_t p0 = (static_cast<uint16_t>(line[0]) << 2) | (line[4] & 0x03u);
                const uint16_t p1 = (static_cast<uint16_t>(line[1]) << 2) | ((line[4] >> 2) & 0x03u);
                const uint16_t p2 = (static_cast<uint16_t>(line[2]) << 2) | ((line[4] >> 4) & 0x03u);
                const uint16_t p3 = (static_cast<uint16_t>(line[3]) << 2) | ((line[4] >> 6) & 0x03u);
                line += 5;

                const auto to_u8 = [](uint16_t v) -> uint8_t {
                    return static_cast<uint8_t>(v >> 2);
                };
                if (col     < frame.width) bgr_img.at<cv::Vec3b>(row, col + 0) = cv::Vec3b(to_u8(p0), to_u8(p0), to_u8(p0));
                if (col + 1 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 1) = cv::Vec3b(to_u8(p1), to_u8(p1), to_u8(p1));
                if (col + 2 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 2) = cv::Vec3b(to_u8(p2), to_u8(p2), to_u8(p2));
                if (col + 3 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 3) = cv::Vec3b(to_u8(p3), to_u8(p3), to_u8(p3));
            }
        }
#else
        for (int row = 0; row < frame.height; ++row) {
            const uint8_t* line = raw + row * stride;
            for (int col = 0; col < frame.width; col += 4) {
                const uint16_t p0 = (static_cast<uint16_t>(line[0]) << 2) | (line[4] & 0x03u);
                const uint16_t p1 = (static_cast<uint16_t>(line[1]) << 2) | ((line[4] >> 2) & 0x03u);
                const uint16_t p2 = (static_cast<uint16_t>(line[2]) << 2) | ((line[4] >> 4) & 0x03u);
                const uint16_t p3 = (static_cast<uint16_t>(line[3]) << 2) | ((line[4] >> 6) & 0x03u);
                line += 5;

                const auto to_u8 = [](uint16_t v) -> uint8_t {
                    return static_cast<uint8_t>(v >> 2);
                };
                if (col     < frame.width) bgr_img.at<cv::Vec3b>(row, col + 0) = cv::Vec3b(to_u8(p0), to_u8(p0), to_u8(p0));
                if (col + 1 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 1) = cv::Vec3b(to_u8(p1), to_u8(p1), to_u8(p1));
                if (col + 2 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 2) = cv::Vec3b(to_u8(p2), to_u8(p2), to_u8(p2));
                if (col + 3 < frame.width) bgr_img.at<cv::Vec3b>(row, col + 3) = cv::Vec3b(to_u8(p3), to_u8(p3), to_u8(p3));
            }
        }
#endif
        return bgr_img;
    }

    (void)target_format;
    return cv::Mat();
}

bool CameraWrapper::set_exposure(int exposure_us) {
    (void)exposure_us;
    return impl_ != nullptr;
}

bool CameraWrapper::set_gain(float gain) {
    (void)gain;
    return impl_ != nullptr;
}

// =============================================================================
// Frame Authentication Implementation (ICD-001 / AM7-L2-SEC-001)
// =============================================================================

namespace {
// Default HMAC key for development (should be loaded from config in production)
// This is a 256-bit key stored in .rodata
constexpr const char* kDefaultHmacKey = "AURORE_MK7_FRAME_AUTH_KEY_256BIT_SECRET";

/**
 * @brief Compute frame header for HMAC input.
 * 
 * Packs the frame header fields into a contiguous buffer for HMAC computation.
 * Per ICD-001: HMAC covers header + frame_hash.
 */
void compute_frame_header(const ZeroCopyFrame& frame, uint8_t* out_header, size_t& out_size) {
    // Header layout (matches ICD-001 spec):
    // - sequence:      u64 (8 bytes)
    // - timestamp_ns:  u64 (8 bytes)
    // - exposure_us:   u64 (8 bytes)
    // - gain:          f32 (4 bytes)
    // - width:         u32 (4 bytes)
    // - height:        u32 (4 bytes)
    // - format:        u32 (4 bytes)
    // - buffer_id:     u32 (4 bytes)
    // Total: 44 bytes
    
    size_t offset = 0;
    std::memcpy(out_header + offset, &frame.sequence, sizeof(frame.sequence));
    offset += sizeof(frame.sequence);
    std::memcpy(out_header + offset, &frame.timestamp_ns, sizeof(frame.timestamp_ns));
    offset += sizeof(frame.timestamp_ns);
    std::memcpy(out_header + offset, &frame.exposure_us, sizeof(frame.exposure_us));
    offset += sizeof(frame.exposure_us);
    std::memcpy(out_header + offset, &frame.gain, sizeof(frame.gain));
    offset += sizeof(frame.gain);
    std::memcpy(out_header + offset, &frame.width, sizeof(frame.width));
    offset += sizeof(frame.width);
    std::memcpy(out_header + offset, &frame.height, sizeof(frame.height));
    offset += sizeof(frame.height);
    std::memcpy(out_header + offset, &frame.format, sizeof(frame.format));
    offset += sizeof(frame.format);
    std::memcpy(out_header + offset, &frame.buffer_id, sizeof(frame.buffer_id));
    offset += sizeof(frame.buffer_id);
    
    out_size = offset;
}
}  // anonymous namespace

/**
 * @brief Compute SHA256 hash of frame pixel data.
 * 
 * @param frame Frame to hash
 * @return true if hash computed successfully
 */
bool compute_frame_hash(ZeroCopyFrame& frame) {
    if (!frame.is_valid() || frame.plane_data[0] == nullptr || frame.plane_size[0] == 0) {
        return false;
    }
    
    // Compute SHA256 of pixel data (plane 0 only for RAW10)
    // This is synchronous for simplicity; async version available via AsyncFrameAuthenticator
    aurore::security::compute_sha256_raw_threadsafe(
        frame.plane_data[0], 
        frame.plane_size[0], 
        frame.frame_hash
    );
    
    return true;
}

/**
 * @brief Compute HMAC-SHA256 over frame header + hash.
 * 
 * @param frame Frame to authenticate (must have frame_hash computed)
 * @param hmac_key HMAC key (256-bit recommended)
 * @param key_len Length of key in bytes
 * @return true if HMAC computed successfully
 */
bool compute_frame_hmac(ZeroCopyFrame& frame, const void* hmac_key, size_t key_len) {
    if (!frame.is_valid()) {
        return false;
    }
    
    // Build header buffer
    uint8_t header_buf[64];  // 44 bytes needed
    size_t header_size = 0;
    compute_frame_header(frame, header_buf, header_size);
    
    // Compute HMAC over header + frame_hash
    // Input: header (44 bytes) + frame_hash (32 bytes) = 76 bytes
    std::vector<uint8_t> hmac_input;
    hmac_input.reserve(header_size + 32);
    hmac_input.insert(hmac_input.end(), header_buf, header_buf + header_size);
    hmac_input.insert(hmac_input.end(), frame.frame_hash, frame.frame_hash + 32);
    
    std::string key_str(static_cast<const char*>(hmac_key), key_len);
    aurore::security::compute_hmac_sha256_raw_threadsafe(
        key_str,
        hmac_input.data(),
        hmac_input.size(),
        frame.hmac
    );
    
    return true;
}

/**
 * @brief Authenticate frame (compute hash + HMAC).
 * 
 * This is the main entry point for frame authentication.
 * Called after frame capture, before releasing to consumer.
 * 
 * @param frame Frame to authenticate
 * @param hmac_key HMAC key (nullptr uses default key)
 * @param key_len Length of key (0 uses default key length)
 * @return true if authentication successful
 */
bool authenticate_frame(ZeroCopyFrame& frame, const void* hmac_key, size_t key_len) {
    // Compute SHA256 hash of pixel data
    if (!compute_frame_hash(frame)) {
        return false;
    }
    
    // Use default key if none provided
    if (!hmac_key || key_len == 0) {
        hmac_key = kDefaultHmacKey;
        key_len = std::strlen(kDefaultHmacKey);
    }
    
    // Compute HMAC over header + hash
    return compute_frame_hmac(frame, hmac_key, key_len);
}

/**
 * @brief Verify frame authentication.
 * 
 * Member function implementation for ZeroCopyFrame::verify_authentication.
 * 
 * @param key HMAC key
 * @param key_len Length of key
 * @return true if verification passes
 */
bool ZeroCopyFrame::verify_authentication(const void* key, size_t key_len) const noexcept {
    if (!is_valid()) {
        return false;
    }
    
    // Rebuild header
    uint8_t header_buf[64];
    size_t header_size = 0;
    compute_frame_header(*this, header_buf, header_size);
    
    // Rebuild HMAC input
    std::vector<uint8_t> hmac_input;
    hmac_input.reserve(header_size + 32);
    hmac_input.insert(hmac_input.end(), header_buf, header_buf + header_size);
    hmac_input.insert(hmac_input.end(), frame_hash, frame_hash + 32);
    
    // Verify HMAC
    std::string key_str(static_cast<const char*>(key), key_len);
    return aurore::security::verify_hmac_sha256_raw(key_str, hmac_input.data(), hmac_input.size(), hmac);
}

// =============================================================================
// FrameBufferAllocator (stub — used by tests; real allocation in init_libcamera)
// =============================================================================

bool FrameBufferAllocator::allocate(int width, int height, PixelFormat format, int count) {
    width_  = width;
    height_ = height;
    format_ = format;
    count_  = count;

    switch (format) {
        case PixelFormat::RAW10:
            stride_[0]     = width * 2;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            break;
        case PixelFormat::BGR888:
        case PixelFormat::RGB888:
            stride_[0]     = width * 3;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            break;
        case PixelFormat::NV12:
            stride_[0]     = width;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            stride_[1]     = width;
            plane_size_[1] = static_cast<size_t>(stride_[1]) * static_cast<size_t>(height) / 2u;
            break;
        case PixelFormat::YUV420:
            stride_[0]     = width;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            stride_[1]     = width / 2;
            plane_size_[1] = static_cast<size_t>(stride_[1]) * static_cast<size_t>(height) / 2u;
            stride_[2]     = width / 2;
            plane_size_[2] = static_cast<size_t>(stride_[2]) * static_cast<size_t>(height) / 2u;
            break;
    }

    buffers_.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        buffers_[static_cast<size_t>(i)].fd   = -1;
        buffers_[static_cast<size_t>(i)].data = nullptr;
        buffers_[static_cast<size_t>(i)].size = plane_size_[0];
    }
    return true;
}

void FrameBufferAllocator::free() {
    for (auto& buffer : buffers_) {
        if (buffer.data) { munmap(buffer.data, buffer.size); }
        if (buffer.fd >= 0) { close(buffer.fd); }
    }
    buffers_.clear();
}

void* FrameBufferAllocator::get_data(int index, int /*plane*/) {
    if (index < 0 || index >= count_) { return nullptr; }
    return buffers_[static_cast<size_t>(index)].data;
}

}  // namespace aurore
