// Simple V4L2 camera capture for 120 FPS
// Focus: Get frames at 120 FPS, pass to existing pipeline

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>

#define NUM_BUFFERS 8

struct Buffer {
    void* start;
    size_t length;
};

static struct Buffer buffers_[NUM_BUFFERS];
static int fd_ = -1;
static int running_ = 0;
static int frame_count_ = 0;
static int64_t start_time_ = 0;

static int64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int v4l2_open(const char* device, int* out_fd) {
    *out_fd = open(device, O_RDWR | O_NONBLOCK, 0);
    if (*out_fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", device, strerror(errno));
        return -1;
    }
    return 0;
}

int v4l2_query_cap(int fd) {
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "VIDIOC_QUERYCAP failed: %s\n", strerror(errno));
        return -1;
    }
    
    printf("V4L2: Driver: %s\n", cap.driver);
    printf("V4L2: Card: %s\n", cap.card);
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "V4L2: No video capture support\n");
        return -1;
    }
    
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "V4L2: No streaming support\n");
        return -1;
    }
    
    return 0;
}

int v4l2_enum_formats(int fd) {
    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    printf("V4L2: Available formats:\n");
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        printf("  %d: %s (0x%08x)\n", fmtdesc.index, fmtdesc.description, fmtdesc.pixelformat);
        
        // Check frame sizes
        struct v4l2_frmsizeenum frmsize = {};
        frmsize.pixel_format = fmtdesc.pixelformat;
        while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("    %dx%d\n", frmsize.discrete.width, frmsize.discrete.height);
            } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
                printf("    %dx%d to %dx%d (step %dx%d)\n",
                       frmsize.stepwise.min_width, frmsize.stepwise.min_height,
                       frmsize.stepwise.max_width, frmsize.stepwise.max_height,
                       frmsize.stepwise.step_width, frmsize.stepwise.step_height);
            }
            frmsize.index++;
        }
        
        fmtdesc.index++;
    }
    
    return 0;
}

int v4l2_set_format(int fd, int width, int height, uint32_t pixelformat) {
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "VIDIOC_S_FMT failed: %s\n", strerror(errno));
        return -1;
    }
    
    printf("V4L2: Format set to %dx%d (0x%08x)\n", 
           fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);
    
    return 0;
}

int v4l2_set_fps(int fd, int fps) {
    struct v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    
    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
        fprintf(stderr, "VIDIOC_S_PARM failed: %s\n", strerror(errno));
        fprintf(stderr, "Note: rp1-cfe driver may not support FPS control via V4L2\n");
        fprintf(stderr, "FPS is determined by sensor mode and libcamera PiSP handler\n");
        return -1;  // Continue anyway, let format determine FPS
    }
    
    printf("V4L2: FPS set to %d\n", fps);
    return 0;
}

int v4l2_request_buffers(int fd) {
    struct v4l2_requestbuffers req = {};
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (req.count < NUM_BUFFERS) {
        fprintf(stderr, "V4L2: Not enough buffers: %d\n", req.count);
        return -1;
    }
    
    return 0;
}

int v4l2_queue_buffers(int fd) {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QUERYBUF failed: %s\n", strerror(errno));
            return -1;
        }
        
        buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, buf.m.offset);
        buffers_[i].length = buf.length;
        
        if (buffers_[i].start == MAP_FAILED) {
            fprintf(stderr, "mmap failed: %s\n", strerror(errno));
            return -1;
        }
        
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF failed: %s\n", strerror(errno));
            return -1;
        }
    }
    
    return 0;
}

int v4l2_start(int fd) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "VIDIOC_STREAMON failed: %s\n", strerror(errno));
        return -1;
    }
    
    start_time_ = get_time_ns();
    return 0;
}

int v4l2_stop(int fd) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // Unmap buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (buffers_[i].start && buffers_[i].start != MAP_FAILED) {
            munmap(buffers_[i].start, buffers_[i].length);
        }
    }
    
    return 0;
}

int v4l2_capture_frame(int fd, void** out_data, size_t* out_length, int64_t* out_timestamp) {
    struct pollfd fds = { fd, POLLIN, 0 };
    
    int ret = poll(&fds, 1, 100);  // 100ms timeout
    if (ret < 0) {
        if (errno == EINTR) return 0;
        fprintf(stderr, "poll failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (ret == 0) {
        return 0;  // Timeout
    }
    
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN || errno == EINTR) return 0;
        fprintf(stderr, "VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return -1;
    }
    
    // Return frame data
    *out_data = buffers_[buf.index].start;
    *out_length = buf.bytesused;
    *out_timestamp = (int64_t)buf.timestamp.tv_sec * 1000000000LL + buf.timestamp.tv_usec * 1000LL;
    frame_count_++;
    
    // Re-queue buffer
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "VIDIOC_QBUF failed: %s\n", strerror(errno));
        return -1;
    }
    
    // Print FPS every 30 frames
    if (frame_count_ % 30 == 0) {
        int64_t now = get_time_ns();
        double elapsed_s = (now - start_time_) / 1000000000.0;
        double fps = frame_count_ / elapsed_s;
        printf("V4L2: FPS: %.1f (frames: %d)\n", fps, frame_count_);
    }
    
    return 1;
}

void v4l2_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

// Test program
int main() {
    printf("=== V4L2 Camera Test for 120 FPS ===\n\n");
    
    // Try to open camera
    const char* devices[] = {"/dev/video0", "/dev/video10", "/dev/video20"};
    
    for (int i = 0; i < 3; i++) {
        if (v4l2_open(devices[i], &fd_) == 0) {
            printf("Opened %s\n\n", devices[i]);
            break;
        }
    }
    
    if (fd_ < 0) {
        fprintf(stderr, "No camera device found\n");
        return 1;
    }
    
    // Query capabilities
    if (v4l2_query_cap(fd_) < 0) {
        v4l2_close(fd_);
        return 1;
    }
    
    printf("\n");
    
    // Enumerate formats
    v4l2_enum_formats(fd_);
    
    printf("\n");
    
    // Try to set 1280x720 @ 120 FPS
    // Format: V4L2_PIX_FMT_RGB24 or V4L2_PIX_FMT_SBGGR10 (Bayer)
    if (v4l2_set_format(fd_, 1280, 720, V4L2_PIX_FMT_RGB24) < 0) {
        // Try different format
        v4l2_set_format(fd_, 1280, 720, V4L2_PIX_FMT_SBGGR10);
    }
    
    v4l2_set_fps(fd_, 120);
    v4l2_set_fps(fd_, 120);  // Call again for logging
    
    printf("\n");
    
    // Request and queue buffers
    if (v4l2_request_buffers(fd_) < 0) {
        v4l2_close(fd_);
        return 1;
    }
    
    if (v4l2_queue_buffers(fd_) < 0) {
        v4l2_close(fd_);
        return 1;
    }
    
    // Start capture
    if (v4l2_start(fd_) < 0) {
        v4l2_close(fd_);
        return 1;
    }
    
    printf("\n=== Capturing for 10 seconds ===\n");
    printf("(Note: FPS is determined by sensor mode, not V4L2 controls)\n\n");
    
    // Capture loop for 10 seconds
    int64_t start = get_time_ns();
    void* data;
    size_t length;
    int64_t timestamp;
    int captured = 0;
    
    while (get_time_ns() - start < 10000000000LL) {  // 10 seconds
        int ret = v4l2_capture_frame(fd_, &data, &length, &timestamp);
        if (ret > 0) {
            captured++;
            // Process frame here
        }
    }
    
    printf("\n=== Results ===\n");
    printf("Frames captured: %d\n", captured);
    
    v4l2_stop(fd_);
    v4l2_close(fd_);
    
    return 0;
}
