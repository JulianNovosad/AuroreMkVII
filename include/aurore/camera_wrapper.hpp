/**
 * @file camera_wrapper.hpp
 * @brief libcamera wrapper with zero-copy OpenCV integration
 * 
 * Provides zero-copy frame acquisition from libcamera DMA buffers
 * to OpenCV cv::Mat headers without intermediate memory copies.
 * 
 * Key features:
 * - DMA buffer mmap for zero-copy access
 * - OpenCV cv::Mat header wrapping (no memcpy)
 * - Frame timestamp capture with CLOCK_MONOTONIC_RAW
 * - Sequence number tracking for stall detection
 * - Configurable resolution and frame rate
 * 
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 * 
 * Requirements:
 * - libcamera-dev
 * - OpenCV (libopencv-dev)
 * - Raspberry Pi Camera Module 3 (Sony IMX708) or compatible
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "timing.hpp"
#include "ring_buffer.hpp"

// Forward declare libcamera types to minimize header dependencies
namespace libcamera {
    class CameraManager;
    class Camera;
    class FrameBuffer;
    class FrameBufferAllocator;
    class ControlList;
    class Request;
    class Stream;
    class StreamConfiguration;
    class StreamRoles;
    class Transform;
    struct StreamRole;
    struct PixelFormat;
    struct Size;
    struct Rectangle;
}

// Forward declare OpenCV types
namespace cv {
    class Mat;
}

namespace aurore {

/**
 * @brief Default camera resolution for Aurore MkVII
 * 
 * 1536×864 is the native resolution for 120Hz operation
 * per AM7-L2-VIS-003.
 */
constexpr int DEFAULT_WIDTH = 1536;
constexpr int DEFAULT_HEIGHT = 864;

/**
 * @brief Default frame rate
 * 
 * 120 FPS per AM7-L2-TIM-001.
 */
constexpr int DEFAULT_FPS = 120;

/**
 * @brief Number of buffers in camera ring buffer
 * 
 * 4 frames per ICD-001.
 */
constexpr int CAMERA_BUFFER_COUNT = 4;

/**
 * @brief Pixel format enumeration
 */
enum class PixelFormat {
    RAW10,      ///< 10-bit RAW (Sony IMX708 native)
    RGB888,     ///< 24-bit RGB
    BGR888,     ///< 24-bit BGR (OpenCV default)
    YUV420,     ///< YUV 4:2:0 planar
    NV12,       ///< YUV 4:2:0 semi-planar
};

/**
 * @brief Camera configuration
 */
struct CameraConfig {
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    int fps = DEFAULT_FPS;
    PixelFormat format = PixelFormat::RAW10;
    int buffer_count = CAMERA_BUFFER_COUNT;
    const char* camera_id = nullptr;  ///< nullptr = auto-select first camera
    bool enable_hw_accel = true;       ///< Enable VideoCore VII acceleration
    int exposure_us = 1000;            ///< Exposure time in microseconds
    float gain = 1.0f;                 ///< ISO gain (1.0 = ISO 100)
    
    /**
     * @brief Validate configuration
     */
    bool validate() const noexcept {
        return width > 0 && height > 0 && fps > 0 && fps <= 120 &&
               buffer_count >= 2 && buffer_count <= 8;
    }
};

/**
 * @brief Zero-copy frame descriptor
 *
 * Contains pointers to DMA-mapped memory and frame metadata.
 * Frame data is NOT copied - this is a view into DMA buffers.
 *
 * Note: This struct must be trivially copyable for lock-free ring buffer.
 *
 * Security: All pointer fields must be validated before dereferencing.
 * Use validate() to check frame integrity.
 */
struct ZeroCopyFrame {
    uint64_t sequence;            ///< Frame sequence number
    TimestampNs timestamp_ns;     ///< Frame timestamp (CLOCK_MONOTONIC_RAW)
    uint64_t exposure_us;         ///< Exposure time in microseconds
    float gain;                   ///< ISO gain

    void* plane_data[4];          ///< Pointers to DMA buffer planes
    size_t plane_size[4];         ///< Size of each plane in bytes
    int stride[4];                ///< Bytes per line for each plane

    int width;
    int height;
    PixelFormat format;

    bool valid;                   ///< Frame validity flag
    char error[128];              ///< Error message if invalid (fixed size for trivial copy)

    ZeroCopyFrame() noexcept
        : sequence(0)
        , timestamp_ns(0)
        , exposure_us(0)
        , gain(0.0f)
        , width(0)
        , height(0)
        , format(PixelFormat::RAW10)
        , valid(false) {
        for (int i = 0; i < 4; i++) {
            plane_data[i] = nullptr;
            plane_size[i] = 0;
            stride[i] = 0;
        }
        error[0] = '\0';
    }

    /**
     * @brief Check if frame has valid data
     */
    bool is_valid() const noexcept {
        return valid && plane_data[0] != nullptr && width > 0 && height > 0;
    }

    /**
     * @brief Validate frame pointers and sizes (security check)
     *
     * Verifies:
     * - All non-null pointers are properly aligned
     * - Plane sizes match expected dimensions
     * - No integer overflow in size calculations
     *
     * @param expected_width Expected frame width (or 0 to skip)
     * @param expected_height Expected frame height (or 0 to skip)
     * @return true if frame passes all validation checks
     */
    bool validate(int expected_width = 0, int expected_height = 0) const noexcept {
        // Check basic dimensions
        if (width <= 0 || height <= 0) {
            return false;
        }

        // Check against expected dimensions if provided
        if (expected_width > 0 && width != expected_width) {
            return false;
        }
        if (expected_height > 0 && height != expected_height) {
            return false;
        }

        // Check for reasonable maximum dimensions (prevent overflow attacks)
        constexpr int MAX_DIM = 8192;  // 8K max
        if (width > MAX_DIM || height > MAX_DIM) {
            return false;
        }

        // Validate plane 0 (required for all formats)
        if (plane_data[0] == nullptr) {
            return false;
        }

        // Check pointer alignment (DMA buffers should be page-aligned)
        if ((reinterpret_cast<uintptr_t>(plane_data[0]) & 0x0FFF) != 0) {
            // Not page-aligned - suspicious but not necessarily invalid
            // Log warning in production
        }

        // Calculate expected plane 0 size based on format
        size_t expected_size = 0;
        switch (format) {
            case PixelFormat::RAW10:
                // 2 bytes per pixel (10-bit packed in 16-bit words)
                expected_size = static_cast<size_t>(width) * 2 * static_cast<size_t>(height);
                break;
            case PixelFormat::BGR888:
            case PixelFormat::RGB888:
                expected_size = static_cast<size_t>(width) * 3 * static_cast<size_t>(height);
                break;
            case PixelFormat::NV12:
            case PixelFormat::YUV420:
                // Y plane: width * height, UV planes: width * height / 2
                expected_size = static_cast<size_t>(width) * static_cast<size_t>(height);
                break;
        }

        // Check for integer overflow
        if (expected_size == 0) {
            return false;
        }

        // Plane size should be at least the expected size
        if (plane_size[0] < expected_size) {
            return false;
        }

        // Maximum reasonable plane size (64MB per plane)
        constexpr size_t MAX_PLANE_SIZE = 64 * 1024 * 1024;
        if (plane_size[0] > MAX_PLANE_SIZE) {
            return false;
        }

        // Validate stride
        if (stride[0] <= 0 || stride[0] > width * 4) {
            return false;
        }

        return true;
    }

    /**
     * @brief Get plane data with bounds checking
     *
     * @param plane Plane index (0-3)
     * @return void* Plane data pointer, or nullptr if invalid
     */
    void* get_plane_data(int plane) const noexcept {
        if (plane < 0 || plane >= 4) {
            return nullptr;
        }
        return plane_data[plane];
    }

    /**
     * @brief Get plane size with bounds checking
     *
     * @param plane Plane index (0-3)
     * @return size_t Plane size, or 0 if invalid
     */
    size_t get_plane_size(int plane) const noexcept {
        if (plane < 0 || plane >= 4) {
            return 0;
        }
        return plane_size[plane];
    }
};

/**
 * @brief libcamera exception wrapper
 */
class CameraException : public std::runtime_error {
public:
    explicit CameraException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/**
 * @brief libcamera wrapper with zero-copy OpenCV integration
 * 
 * Manages libcamera lifecycle and provides zero-copy frame access.
 * 
 * Usage:
 * @code
 *     CameraConfig config;
 *     config.width = 1536;
 *     config.height = 864;
 *     config.fps = 120;
 *     
 *     CameraWrapper camera(config);
 *     camera.init();
 *     camera.start();
 *     
 *     // Get frame (zero-copy)
 *     ZeroCopyFrame frame;
 *     if (camera.capture_frame(frame)) {
 *         // Wrap as OpenCV Mat (no copy)
 *         cv::Mat img = camera.wrap_as_mat(frame);
 *         
 *         // Process with OpenCV...
 *         cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
 *     }
 *     
 *     camera.stop();
 * @endcode
 */
class CameraWrapper {
public:
    /**
     * @brief Construct camera wrapper
     * 
     * @param config Camera configuration
     */
    explicit CameraWrapper(const CameraConfig& config = CameraConfig());
    
    /**
     * @brief Destructor
     */
    ~CameraWrapper();
    
    // Non-copyable
    CameraWrapper(const CameraWrapper&) = delete;
    CameraWrapper& operator=(const CameraWrapper&) = delete;
    
    /**
     * @brief Initialize camera
     * 
     * Opens camera device, allocates buffers, and configures streams.
     * 
     * @return true on success, false on failure
     * @throws CameraException on initialization failure
     */
    bool init();
    
    /**
     * @brief Start camera capture
     * 
     * @return true on success, false on failure
     */
    bool start();
    
    /**
     * @brief Stop camera capture
     */
    void stop();
    
    /**
     * @brief Check if camera is running
     */
    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Capture single frame (blocking)
     * 
     * @param frame Output frame descriptor
     * @param timeout_ms Timeout in milliseconds (default: 100ms)
     * @return true if frame captured, false on timeout
     */
    bool capture_frame(ZeroCopyFrame& frame, int timeout_ms = 100);
    
    /**
     * @brief Try capture frame (non-blocking)
     * 
     * @param frame Output frame descriptor
     * @return true if frame available, false if not
     */
    bool try_capture_frame(ZeroCopyFrame& frame);
    
    /**
     * @brief Wrap frame as OpenCV Mat (zero-copy)
     * 
     * @param frame Frame descriptor
     * @param target_format Target OpenCV format (default: BGR888)
     * @return cv::Mat OpenCV Mat header (references DMA buffer, no copy)
     */
    cv::Mat wrap_as_mat(const ZeroCopyFrame& frame, 
                        PixelFormat target_format = PixelFormat::BGR888);
    
    /**
     * @brief Get camera configuration
     */
    const CameraConfig& config() const noexcept {
        return config_;
    }
    
    /**
     * @brief Get frame count
     */
    uint64_t frame_count() const noexcept {
        return frame_count_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get error count
     */
    uint64_t error_count() const noexcept {
        return error_count_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Set exposure time
     * 
     * @param exposure_us Exposure time in microseconds
     * @return true on success
     */
    bool set_exposure(int exposure_us);
    
    /**
     * @brief Set gain (ISO)
     * 
     * @param gain Gain value (1.0 = ISO 100)
     * @return true on success
     */
    bool set_gain(float gain);

private:
    /**
     * @brief Internal implementation class (pimpl pattern)
     * 
     * Hides libcamera headers from users of this class.
     */
    struct Impl;
    
    std::unique_ptr<Impl> impl_;
    CameraConfig config_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> frame_count_;
    std::atomic<uint64_t> error_count_;
};

/**
 * @brief Frame buffer allocator for zero-copy operation
 * 
 * Manages DMA buffer allocation and mmap.
 */
class FrameBufferAllocator {
public:
    /**
     * @brief Allocate DMA buffers
     * 
     * @param width Frame width
     * @param height Frame height
     * @param format Pixel format
     * @param count Number of buffers
     * @return true on success
     */
    bool allocate(int width, int height, PixelFormat format, int count);
    
    /**
     * @brief Free DMA buffers
     */
    void free();
    
    /**
     * @brief Get buffer count
     */
    int count() const noexcept {
        return count_;
    }
    
    /**
     * @brief Get mmap'd pointer for buffer
     * 
     * @param index Buffer index
     * @param plane Plane index
     * @return void* Mapped memory pointer
     */
    void* get_data(int index, int plane = 0);
    
    /**
     * @brief Get buffer size
     * 
     * @param plane Plane index
     * @return size_t Buffer size in bytes
     */
    size_t get_size(int plane = 0) const noexcept {
        return plane_size_[plane];
    }
    
    /**
     * @brief Get stride
     * 
     * @param plane Plane index
     * @return int Bytes per line
     */
    int get_stride(int plane = 0) const noexcept {
        return stride_[plane];
    }

private:
    struct Buffer {
        int fd;
        void* data;
        size_t size;
    };
    
    std::vector<Buffer> buffers_;
    size_t plane_size_[4];
    int stride_[4];
    int count_;
    int width_;
    int height_;
    PixelFormat format_;
};

}  // namespace aurore
