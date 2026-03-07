#include "camera/v4l2_camera.h"
#include "util_logging.h"
#include "application.h"
#include "timing.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <drm_fourcc.h>

V4L2CameraCapture::V4L2CameraCapture(const V4L2CameraConfig& config,
                                     std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool,
                                     std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                                     ImageQueue& image_processor_input_queue,
                                     std::chrono::seconds watchdog_timeout)
    : config_(config)
    , fd_(-1)
    , image_buffer_pool_(image_buffer_pool)
    , image_data_pool_(image_data_pool)
    , image_processor_input_queue_(image_processor_input_queue)
    , watchdog_timeout_(watchdog_timeout)
    , frame_count_(0)
    , frame_id_(0)
{
    // Default device path for Raspberry Pi camera
    device_path_ = "/dev/video0";
}

V4L2CameraCapture::~V4L2CameraCapture() {
    stop();
}

bool V4L2CameraCapture::open_device() {
    fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        APP_LOG_ERROR("V4L2Camera: Failed to open " + device_path_ + ": " + strerror(errno));
        return false;
    }
    APP_LOG_INFO("V4L2Camera: Opened " + device_path_);
    return true;
}

void V4L2CameraCapture::close_device() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
        APP_LOG_INFO("V4L2Camera: Closed device");
    }
}

bool V4L2CameraCapture::query_device() {
    struct v4l2_capability cap;
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        APP_LOG_ERROR("V4L2Camera: VIDIOC_QUERYCAP failed: " + std::string(strerror(errno)));
        return false;
    }

    APP_LOG_INFO("V4L2Camera: Driver: " + std::string((char*)cap.driver));
    APP_LOG_INFO("V4L2Camera: Card: " + std::string((char*)cap.card));
    APP_LOG_INFO("V4L2Camera: Bus: " + std::string((char*)cap.bus_info));
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        APP_LOG_ERROR("V4L2Camera: No video capture support");
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        APP_LOG_ERROR("V4L2Camera: No streaming support");
        return false;
    }

    // Check for DMABUF import support
    if (cap.capabilities & V4L2_CAP_DEVICE_CAPS) {
        APP_LOG_INFO("V4L2Camera: Device caps supported");
    }

    return true;
}

bool V4L2CameraCapture::set_sensor_mode(unsigned int mode) {
    // IMX708 modes:
    // Mode 0: 4608x2592 @ 14 FPS
    // Mode 1: 2304x1296 @ 30 FPS
    // Mode 2: 1536x864 @ 60 FPS
    // Mode 3: 1280x720 @ 120 FPS ← TARGET
    
    // Try to set sensor mode via V4L2_CID_CAMERA_SENSOR_MODE
    // This control may not be available in all kernels
#ifdef V4L2_CID_CAMERA_SENSOR_MODE
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_CAMERA_SENSOR_MODE;
    ctrl.value = mode;

    if (ioctl(fd_, VIDIOC_S_CTRL, &ctrl) >= 0) {
        APP_LOG_INFO("V4L2Camera: Set sensor mode to " + std::to_string(mode));
        return true;
    }
#endif

    // Fallback: Check frame intervals for high FPS modes
    struct v4l2_frmivalenum frmival = {};
    frmival.pixel_format = config_.pixel_format;
    frmival.width = config_.width;
    frmival.height = config_.height;
    
    APP_LOG_INFO("V4L2Camera: Checking frame intervals for " + 
                 std::to_string(config_.width) + "x" + std::to_string(config_.height));
    
    bool found_120fps = false;
    for (int i = 0; ; i++) {
        frmival.index = i;
        if (ioctl(fd_, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) < 0) {
            break;
        }
        
        if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            double fps = 1.0 / (frmival.discrete.numerator / (double)frmival.discrete.denominator);
            APP_LOG_INFO("V4L2Camera: Frame interval " + std::to_string(i) + 
                         ": " + std::to_string(frmival.discrete.numerator) + "/" + 
                         std::to_string(frmival.discrete.denominator) + 
                         " = " + std::to_string(fps) + " FPS");
            
            if (fps >= 119.0) {
                found_120fps = true;
                APP_LOG_INFO("V4L2Camera: Found 120 FPS mode!");
            }
        }
    }
    
    if (found_120fps) {
        return true;
    }
    
    APP_LOG_WARNING("V4L2Camera: 120 FPS mode not available, current sensor mode may limit FPS");
    return true;  // Continue anyway, let format negotiation determine actual FPS
}

bool V4L2CameraCapture::set_format() {
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = config_.pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        APP_LOG_ERROR("VIDIOC_S_FMT failed: " + std::string(strerror(errno)));
        return false;
    }

    // Verify format was accepted
    if (fmt.fmt.pix.width != config_.width || fmt.fmt.pix.height != config_.height) {
        APP_LOG_WARNING("V4L2Camera: Format adjusted to " + 
                       std::to_string(fmt.fmt.pix.width) + "x" + std::to_string(fmt.fmt.pix.height));
    }

    APP_LOG_INFO("V4L2Camera: Format set to " + 
                 std::to_string(fmt.fmt.pix.width) + "x" + std::to_string(fmt.fmt.pix.height) +
                 " format: 0x" + std::to_string((unsigned long)fmt.fmt.pix.pixelformat));

    return true;
}

bool V4L2CameraCapture::set_params() {
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = config_.fps;

    if (ioctl(fd_, VIDIOC_S_PARM, &parm) < 0) {
        APP_LOG_WARNING("V4L2Camera: VIDIOC_S_PARM failed: " + std::string(strerror(errno)));
    }

    APP_LOG_INFO("V4L2Camera: Set FPS to " + std::to_string(config_.fps));
    return true;
}

bool V4L2CameraCapture::request_buffers() {
    struct v4l2_requestbuffers req = {};
    req.count = config_.buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        APP_LOG_ERROR("VIDIOC_REQBUFS failed: " + std::string(strerror(errno)));
        return false;
    }

    if (req.count < config_.buffer_count) {
        APP_LOG_ERROR("V4L2Camera: Not enough buffers: " + std::to_string(req.count));
        return false;
    }

    buffers_.resize(req.count);
    
    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            APP_LOG_ERROR("VIDIOC_QUERYBUF failed: " + std::string(strerror(errno)));
            return false;
        }

        buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd_, buf.m.offset);
        buffers_[i].length = buf.length;
        buffers_[i].dmabuf_fd = -1;

        if (buffers_[i].start == MAP_FAILED) {
            APP_LOG_ERROR("mmap failed: " + std::string(strerror(errno)));
            return false;
        }

        APP_LOG_INFO("V4L2Camera: Buffer " + std::to_string(i) + 
                     " mapped: " + std::to_string(buf.length) + " bytes");

        if (export_dmabuf(i, &buffers_[i].dmabuf_fd)) {
            APP_LOG_INFO("V4L2Camera: Buffer " + std::to_string(i) + 
                         " exported to DMA-BUF fd: " + std::to_string(buffers_[i].dmabuf_fd));
        }
    }

    APP_LOG_INFO("V4L2Camera: Allocated " + std::to_string(req.count) + " buffers");
    return true;
}

bool V4L2CameraCapture::queue_buffers() {
    for (unsigned int i = 0; i < buffers_.size(); i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            APP_LOG_ERROR("VIDIOC_QBUF failed: " + std::string(strerror(errno)));
            return false;
        }
        queued_buffers_.push_back(i);
    }
    return true;
}

bool V4L2CameraCapture::start_capture() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        APP_LOG_ERROR("VIDIOC_STREAMON failed: " + std::string(strerror(errno)));
        return false;
    }
    APP_LOG_INFO("V4L2Camera: Stream started");
    return true;
}

void V4L2CameraCapture::stop_capture() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);
    APP_LOG_INFO("V4L2Camera: Stream stopped");
}

bool V4L2CameraCapture::export_dmabuf(int index, int* out_fd) {
    struct v4l2_exportbuffer exp = {};
    exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    exp.index = index;
    exp.flags = O_RDONLY;
    
    if (ioctl(fd_, VIDIOC_EXPBUF, &exp) < 0) {
        APP_LOG_WARNING("V4L2Camera: DMABUF export failed: " + std::string(strerror(errno)));
        return false;
    }
    
    *out_fd = exp.fd;
    return true;
}

void V4L2CameraCapture::capture_thread_func() {
    APP_LOG_INFO("V4L2Camera: Capture thread started");
    
    struct pollfd fds[1];
    fds[0].fd = fd_;
    fds[0].events = POLLIN;
    
    first_frame_time_ = std::chrono::steady_clock::now();
    frame_count_ = 0;
    frame_id_ = 0;
    last_activity_ = std::chrono::steady_clock::now();
    
    while (Aurore::atomic_load_acquire(running_)) {
        // Check watchdog timeout
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_) > watchdog_timeout_) {
            APP_LOG_WARNING("V4L2Camera: Watchdog timeout - no frames received");
            break;
        }
        
        int ret = poll(fds, 1, 100);  // 100ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            APP_LOG_ERROR("V4L2Camera: poll failed: " + std::string(strerror(errno)));
            break;
        }
        
        if (ret == 0) {
            continue;  // Timeout, no data
        }
        
        if (fds[0].revents & POLLIN) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            
            if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                APP_LOG_ERROR("VIDIOC_DQBUF failed: " + std::string(strerror(errno)));
                break;
            }
            
            last_activity_ = std::chrono::steady_clock::now();
            
            // Process the buffer
            bool success = process_buffer(buf.index);
            frame_id_++;
            
            // Re-queue buffer
            if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
                APP_LOG_ERROR("VIDIOC_QBUF failed: " + std::string(strerror(errno)));
            }
            
            // FPS calculation
            frame_count_++;
            if (frame_count_ % 30 == 0) {
                auto fps_now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(fps_now - first_frame_time_).count();
                double fps = (frame_count_ * 1000.0) / elapsed;
                frame_rate_.store(static_cast<int>(fps));
                APP_LOG_INFO("V4L2Camera: FPS: " + std::to_string(fps));
            }
        }
    }
    
    APP_LOG_INFO("V4L2Camera: Capture thread stopped");
}

bool V4L2CameraCapture::process_buffer(int index) {
    if (index < 0 || index >= (int)buffers_.size()) {
        return false;
    }
    
    V4L2Buffer& vbuf = buffers_[index];
    
    // Get timestamp from buffer
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    
    if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) >= 0) {
        vbuf.timestamp = buf.timestamp.tv_sec * 1000000000ULL + buf.timestamp.tv_usec * 1000ULL;
    }
    
    auto capture_time = std::chrono::steady_clock::now();
    uint64_t t_capture_ms = get_time_raw_ms();
    
    // Acquire buffer from pool
    auto img_buffer = image_buffer_pool_->acquire();
    if (!img_buffer) {
        APP_LOG_WARNING("V4L2Camera: Image buffer pool exhausted");
        tpu_stream_drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Copy frame data
    size_t copy_size = std::min(vbuf.length, img_buffer->data.size());
    std::memcpy(img_buffer->data.data(), vbuf.start, copy_size);
    
    // Acquire ImageData from pool
    ImageData* img_data = image_data_pool_->acquire();
    if (!img_data) {
        APP_LOG_WARNING("V4L2Camera: ImageData pool exhausted");
        image_buffer_pool_->release(img_buffer);
        tpu_stream_drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    *img_data = ImageData(capture_time, frame_id_);
    img_data->width = config_.width;
    img_data->height = config_.height;
    img_data->stride = config_.width * 3;  // RGB24
    img_data->format = V4L2_PIX_FMT_RGB24;
    img_data->length = copy_size;
    img_data->buffer = img_buffer;
    img_data->t_capture_raw_ms = t_capture_ms;
    img_data->fd = vbuf.dmabuf_fd;
    img_data->offset = 0;
    
    // Push to queue
    if (!image_processor_input_queue_.push(img_data)) {
        APP_LOG_WARNING("V4L2Camera: Queue push failed");
        image_buffer_pool_->release(img_buffer);
        image_data_pool_->release(img_data);
        tpu_stream_drop_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    frames_produced_.fetch_add(1, std::memory_order_relaxed);
    last_frame_timestamp_.store(static_cast<long long>(t_capture_ms));
    
    return true;
}

void V4L2CameraCapture::watchdog_thread_func() {
    APP_LOG_INFO("V4L2Camera: Watchdog thread started");
    
    while (Aurore::atomic_load_acquire(watchdog_running_)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto now = std::chrono::steady_clock::now();
        auto last = Aurore::atomic_load_acquire(last_frame_timestamp_);
        
        if (last == 0) continue;  // Not started yet
        
        auto elapsed = std::chrono::seconds(now.time_since_epoch()) - std::chrono::seconds(last / 1000);
        if (elapsed > watchdog_timeout_) {
            APP_LOG_ERROR("V4L2Camera: Watchdog triggered - no frames for " + 
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()) + "s");
            Aurore::atomic_store_release(running_, false);
        }
    }
    
    APP_LOG_INFO("V4L2Camera: Watchdog thread stopped");
}

bool V4L2CameraCapture::start() {
    if (Aurore::atomic_load_acquire(running_)) {
        APP_LOG_WARNING("V4L2Camera: Already running");
        return true;
    }
    
    // Open device
    if (!open_device()) {
        return false;
    }
    
    // Query capabilities
    if (!query_device()) {
        close_device();
        return false;
    }
    
    // Set sensor mode (try for 120 FPS)
    if (!set_sensor_mode(IMX708_MODE_3)) {
        APP_LOG_WARNING("V4L2Camera: Could not set Mode 3, using format negotiation");
    }
    
    // Set format
    if (!set_format()) {
        close_device();
        return false;
    }
    
    // Set parameters (FPS)
    if (!set_params()) {
        // Non-fatal, continue
    }
    
    // Request buffers
    if (!request_buffers()) {
        close_device();
        return false;
    }
    
    // Queue buffers
    if (!queue_buffers()) {
        // Cleanup buffers
        for (auto& buf : buffers_) {
            if (buf.start && buf.start != MAP_FAILED) {
                munmap(buf.start, buf.length);
            }
        }
        buffers_.clear();
        close_device();
        return false;
    }
    
    // Start stream
    if (!start_capture()) {
        close_device();
        return false;
    }
    
    // Start capture thread
    Aurore::atomic_store_release(running_, true);
    capture_thread_ = std::thread(&V4L2CameraCapture::capture_thread_func, this);
    
    // Start watchdog
    watchdog_running_.store(true);
    watchdog_thread_ = std::thread(&V4L2CameraCapture::watchdog_thread_func, this);
    
    APP_LOG_INFO("V4L2Camera: Started successfully");
    return true;
}

void V4L2CameraCapture::stop() {
    if (!Aurore::atomic_load_acquire(running_)) {
        return;
    }
    
    APP_LOG_INFO("V4L2Camera: Stopping...");
    
    // Stop capture thread
    Aurore::atomic_store_release(running_, false);
    
    // Stop watchdog
    watchdog_running_.store(false);
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }
    
    // Stop capture thread
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    // Stop stream
    stop_capture();
    
    // Cleanup buffers
    for (auto& buf : buffers_) {
        if (buf.start && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
        if (buf.dmabuf_fd >= 0) {
            close(buf.dmabuf_fd);
        }
    }
    buffers_.clear();
    
    // Close device
    close_device();
    
    APP_LOG_INFO("V4L2Camera: Stopped");
}

void V4L2CameraCapture::get_state() const {
    APP_LOG_INFO("V4L2Camera State:");
    APP_LOG_INFO("  Running: " + std::string(Aurore::atomic_load_acquire(running_) ? "Yes" : "No"));
    APP_LOG_INFO("  FPS: " + std::to_string(Aurore::atomic_load_acquire(frame_rate_)));
    APP_LOG_INFO("  Frames: " + std::to_string(Aurore::atomic_load_acquire(frames_produced_)));
    APP_LOG_INFO("  Drops: " + std::to_string(Aurore::atomic_load_acquire(tpu_stream_drop_count_)));
}
