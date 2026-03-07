#ifndef V4L2_CAMERA_CAPTURE_H
#define V4L2_CAMERA_CAPTURE_H

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <string.h>
#include <linux/videodev2.h>
#include <vector>
#include <cstdint>
#include <chrono>
#include <functional>

struct V4L2Frame {
    uint8_t* data;
    size_t size;
    int width;
    int height;
    int stride;
    uint64_t timestamp_ms;
    int sequence;

    V4L2Frame() : data(nullptr), size(0), width(0), height(0), stride(0), timestamp_ms(0), sequence(0) {}
};

class V4L2CameraCapture {
public:
    V4L2CameraCapture(const char* device_path = "/dev/video0");
    ~V4L2CameraCapture();

    bool open(int width = 1280, int height = 720, int fps = 30);
    void close();

    bool capture_frame(V4L2Frame& frame);
    bool release_frame(V4L2Frame& frame);

    bool is_open() const { return fd_ >= 0; }
    int get_width() const { return width_; }
    int get_height() const { return height_; }

private:
    int fd_;
    int width_;
    int height_;
    int fps_;
    int pixelformat_;
    int buffer_count_;
    struct v4l2_buffer* buffers_;
    uint8_t* memory_mapped_[8];

    bool set_format();
    bool request_buffers();
    bool start_streaming();
    void stop_streaming();
};

V4L2CameraCapture::V4L2CameraCapture(const char* device_path)
    : fd_(-1), width_(1280), height_(720), fps_(30), pixelformat_(V4L2_PIX_FMT_YUYV),
      buffer_count_(0), buffers_(nullptr) {
    (void)device_path;
}

V4L2CameraCapture::~V4L2CameraCapture() {
    close();
}

bool V4L2CameraCapture::open(int width, int height, int fps) {
    width_ = width;
    height_ = height;
    fps_ = fps;

    fd_ = ::open("/dev/video0", O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }

    if (!set_format()) {
        close();
        return false;
    }

    if (!request_buffers()) {
        close();
        return false;
    }

    if (!start_streaming()) {
        close();
        return false;
    }

    return true;
}

void V4L2CameraCapture::close() {
    if (fd_ >= 0) {
        stop_streaming();

        if (buffers_) {
            delete[] buffers_;
            buffers_ = nullptr;
        }

        for (int i = 0; i < buffer_count_; i++) {
            if (memory_mapped_[i]) {
                munmap(memory_mapped_[i], 0);
                memory_mapped_[i] = nullptr;
            }
        }

        ::close(fd_);
        fd_ = -1;
    }
}

bool V4L2CameraCapture::set_format() {
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        return false;
    }

    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    return true;
}

bool V4L2CameraCapture::request_buffers() {
    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        return false;
    }

    buffer_count_ = req.count;
    buffers_ = new v4l2_buffer[buffer_count_];

    for (int i = 0; i < buffer_count_; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            return false;
        }

        memory_mapped_[i] = (uint8_t*)mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, fd_, buf.m.offset);
        if (memory_mapped_[i] == MAP_FAILED) {
            return false;
        }

        buffers_[i] = buf;
    }

    return true;
}

bool V4L2CameraCapture::start_streaming() {
    for (int i = 0; i < buffer_count_; i++) {
        if (ioctl(fd_, VIDIOC_QBUF, &buffers_[i]) < 0) {
            return false;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        return false;
    }

    return true;
}

void V4L2CameraCapture::stop_streaming() {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);
}

bool V4L2CameraCapture::capture_frame(V4L2Frame& frame) {
    struct pollfd pfd = {};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 100);
    if (ret <= 0) {
        return false;
    }

    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        return false;
    }

    frame.width = width_;
    frame.height = height_;
    frame.stride = width_ * 2;
    frame.size = buf.bytesused;
    frame.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.sequence = buf.sequence;
    frame.data = memory_mapped_[buf.index];

    buffers_[buf.index] = buf;

    return true;
}

bool V4L2CameraCapture::release_frame(V4L2Frame& frame) {
    (void)frame;
    return true;
}

#endif // V4L2_CAMERA_CAPTURE_H
