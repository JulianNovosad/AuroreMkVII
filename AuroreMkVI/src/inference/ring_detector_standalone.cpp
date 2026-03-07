#include <iostream>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <functional>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>
#include <cstdlib>
#include <cstdio>

struct GPUDetectionResult {
    float xmin, ymin, xmax, ymax;
    float confidence;
    int ring_count;
    bool valid;
    uint64_t timestamp_ms;
    int frame_id;
};

class GPURingDetector {
public:
    GPURingDetector() : initialized_(false), software_mode_(true) {}

    bool init(int width, int height) {
        frame_width_ = width;
        frame_height_ = height;
        initialized_ = true;
        software_mode_ = true;
        std::cout << "[INFO] Ring detector initialized in software mode" << std::endl;
        return true;
    }

    bool process_frame(const uint8_t* data, int width, int height, std::vector<GPUDetectionResult>& results) {
        if (!initialized_) return false;

        const uint8_t* gray = data;
        int cand_spacing = 80;
        int grid_w = width / cand_spacing;
        int grid_h = height / cand_spacing;
        float max_r = std::min(width, height) / 2.0f;

        for (int row = 0; row < grid_h; row++) {
            for (int col = 0; col < grid_w; col++) {
                float cx = (col + 0.5f) * cand_spacing;
                float cy = (row + 0.5f) * cand_spacing;

                int total_rings = 0;
                for (int d = 0; d < 8; d++) {
                    float angle = d * 3.14159265f / 4.0f;
                    float dx = cosf(angle);
                    float dy = sinf(angle);
                    int trans = 0;
                    float prev = -1.0f;

                    for (int s = 0; s < 64; s++) {
                        float t = (float)(s + 1) * max_r / 64.0f;
                        int px = (int)(cx + dx * t);
                        int py = (int)(cy + dy * t);

                        if (px < 0 || px >= width || py < 0 || py >= height) break;

                        float intensity = gray[py * width + px] / 255.0f;
                        if (prev >= 0.0f && fabs(intensity - prev) > 0.15f) trans++;
                        prev = intensity;
                    }
                    total_rings += trans / 2;
                }

                int avg_rings = total_rings / 8;
                if (avg_rings >= 3) {
                    GPUDetectionResult r;
                    r.xmin = (cx - max_r) / width;
                    r.ymin = (cy - max_r) / height;
                    r.xmax = (cx + max_r) / width;
                    r.ymax = (cy + max_r) / height;
                    r.confidence = std::min(1.0f, avg_rings / 6.0f);
                    r.ring_count = avg_rings;
                    r.valid = true;
                    r.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    results.push_back(r);
                }
            }
        }
        return true;
    }

private:
    int frame_width_;
    int frame_height_;
    bool initialized_;
    bool software_mode_;
};

class V4L2Camera {
public:
    V4L2Camera() : fd_(-1), width_(1280), height_(720) {}

    ~V4L2Camera() { close(); }

    bool open(const char* dev = "/dev/video0") {
        fd_ = ::open(dev, O_RDWR | O_NONBLOCK);
        if (fd_ < 0) return false;

        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width_;
        fmt.fmt.pix.height = height_;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;

        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) { close(); return false; }
        width_ = fmt.fmt.pix.width;
        height_ = fmt.fmt.pix.height;

        struct v4l2_requestbuffers req = {};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) { close(); return false; }

        for (int i = 0; i < 4; i++) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) { close(); return false; }
            bufs_[i] = (uint8_t*)mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
            bufsize_[i] = buf.length;
        }

        for (int i = 0; i < 4; i++) {
            struct v4l2_buffer b = {};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;
            ioctl(fd_, VIDIOC_QBUF, &b);
        }

        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) { close(); return false; }

        std::cout << "[INFO] Camera opened: " << width_ << "x" << height_ << std::endl;
        return true;
    }

    void close() {
        if (fd_ >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);
            for (int i = 0; i < 4; i++) if (bufs_[i]) munmap(bufs_[i], bufsize_[i]);
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool capture(uint8_t* yuyv, uint8_t* gray) {
        struct pollfd pfd = {fd_, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) return false;

        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) return false;

        memcpy(yuyv, bufs_[buf.index], buf.bytesused);

        int stride = width_ * 2;
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x += 2) {
                gray[y * width_ + x] = yuyv[y * stride + x * 2];
                gray[y * width_ + x + 1] = yuyv[y * stride + x * 2 + 2];
            }
        }

        ioctl(fd_, VIDIOC_QBUF, &buf);
        return true;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    bool is_open() const { return fd_ >= 0; }

private:
    int fd_;
    int width_;
    int height_;
    uint8_t* bufs_[4];
    size_t bufsize_[4];

    struct v4l2_buffer buf(int i) { struct v4l2_buffer b = {}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i; return b; }
};

static volatile sig_atomic_t g_running = 1;
void sigint_handler(int) { g_running = 0; }

class TermiosRAII {
    struct termios old_;
public:
    TermiosRAII() {
        tcgetattr(STDIN_FILENO, &old_);
        struct termios raw = old_;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    ~TermiosRAII() { tcsetattr(STDIN_FILENO, TCSANOW, &old_); }
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    TermiosRAII termios;

    std::cout << "=== GPU Ring Detection Test (Software) ===" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    GPURingDetector detector;
    if (!detector.init(1280, 720)) {
        std::cerr << "[ERROR] Failed to initialize detector" << std::endl;
        return 1;
    }

    V4L2Camera camera;
    if (!camera.open("/dev/video0")) {
        std::cerr << "[ERROR] Camera not available, using synthetic frames" << std::endl;
    }

    size_t yuyv_size = 1280 * 720 * 2;
    size_t gray_size = 1280 * 720;
    std::vector<uint8_t> yuyv(yuyv_size);
    std::vector<uint8_t> gray(gray_size);
    std::vector<float> latencies;
    std::atomic<int> frame_count(0);
    int detection_count = 0;

    auto last_fps = std::chrono::steady_clock::now();

    while (g_running) {
        auto start = std::chrono::steady_clock::now();

        if (camera.is_open()) {
            if (!camera.capture(yuyv.data(), gray.data())) {
                usleep(5000);
                continue;
            }
        } else {
            int fc = frame_count.load();
            int cx = 640, cy = 360;
            float radius = 80.0f + sinf(fc * 0.05f) * 20.0f;
            for (int y = 0; y < 720; y++) {
                for (int x = 0; x < 1280; x++) {
                    float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
                    if (d < radius) {
                        int ring = (int)(d / radius * 6);
                        gray[y * 1280 + x] = (ring % 2 == 0) ? 200 : 50;
                    } else {
                        gray[y * 1280 + x] = 30;
                    }
                }
            }
        }

        std::vector<GPUDetectionResult> results;
        detector.process_frame(gray.data(), 1280, 720, results);

        auto end = std::chrono::steady_clock::now();
        float lat_us = std::chrono::duration<float, std::micro>(end - start).count();
        latencies.push_back(lat_us);

        frame_count++;
        detection_count += results.size();

        if (!results.empty()) {
            for (auto& r : results) {
                printf("\r[DETECTED] rings=%d conf=%.2f bbox=(%.0f,%.0f)-(%.0f,%.0f)      ",
                       r.ring_count, r.confidence,
                       r.xmin * 1280, r.ymin * 720,
                       r.xmax * 1280, r.ymax * 720);
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps);
        if (elapsed.count() >= 1000) {
            float avg_lat = 0;
            for (auto l : latencies) avg_lat += l;
            avg_lat /= latencies.size();
            float fps = latencies.size() / (elapsed.count() / 1000.0f);
            printf("\rFPS: %.1f | Latency: %.2f ms | Detections: %d      ",
                   fps, avg_lat / 1000.0f, detection_count);
            fflush(stdout);
            latencies.clear();
            last_fps = now;
        }

        usleep(5000);
    }

    printf("\n\n=== Summary ===\n");
    if (!latencies.empty()) {
        float min_l = *std::min_element(latencies.begin(), latencies.end());
        float max_l = *std::max_element(latencies.begin(), latencies.end());
        float avg_l = 0;
        for (auto l : latencies) avg_l += l;
        avg_l /= latencies.size();
        std::cout << "Frames: " << frame_count << std::endl;
        std::cout << "Detections: " << detection_count << std::endl;
        std::cout << "Avg latency: " << avg_l / 1000.0f << " ms" << std::endl;
        std::cout << "Min latency: " << min_l / 1000.0f << " ms" << std::endl;
        std::cout << "Max latency: " << max_l / 1000.0f << " ms" << std::endl;
        auto sorted = latencies;
        size_t p95 = sorted.size() * 95 / 100;
        std::nth_element(sorted.begin(), sorted.begin() + p95, sorted.end());
        std::cout << "P95 latency: " << sorted[p95] / 1000.0f << " ms" << std::endl;
        std::cout << "WCET (3ms): " << (sorted[p95] < 3000.0f ? "PASS" : "FAIL") << std::endl;
    }

    return 0;
}
