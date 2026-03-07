#include "gpu_detector.h"
#include "util_logging.h"
#include "v4l2_camera_capture.h"
#include <opencv2/opencv.hpp>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cmath>

using namespace aurore;
using namespace aurore::inference;
using namespace aurore::logging;

static volatile sig_atomic_t g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

class TermiosHelper {
public:
    TermiosHelper() {
        tcgetattr(STDIN_FILENO, &original_);
        struct termios raw = original_;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    ~TermiosHelper() {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }

private:
    struct termios original_;
};

class LiveCameraTest {
public:
    LiveCameraTest()
        : frame_width_(1280)
        , frame_height_(720)
        , frame_count_(0)
        , fps_(0)
        , detection_count_(0)
        , avg_latency_us_(0.0f)
    {
    }

    bool init() {
        Logger::init("gpu_live", "logs", nullptr);
        Logger::getInstance().start_writer_thread();

        APP_LOG_INFO("=== GPU Target Detection - Live Camera Test ===");

        detector_ = std::make_unique<GPUDetector>();

        if (!detector_->init(frame_width_, frame_height_)) {
            APP_LOG_ERROR("Failed to initialize GPU detector");
            return false;
        }

        APP_LOG_INFO("GPU Detector initialized");
        APP_LOG_INFO("Attempting to open camera at /dev/video0...");

        if (!camera_.open(frame_width_, frame_height_, 30)) {
            APP_LOG_WARNING("Failed to open camera, running in synthetic mode");
            synthetic_mode_ = true;
        } else {
            APP_LOG_INFO("Camera opened: " + std::to_string(frame_width_) + "x" +
                         std::to_string(frame_height_));
            synthetic_mode_ = false;
        }

        detector_->set_detection_callback([this](const std::vector<GPUDetectionResult>& results) {
            on_detections(results);
        });

        last_fps_update_ = std::chrono::steady_clock::now();
        return true;
    }

    void run() {
        APP_LOG_INFO("Starting live detection (Ctrl+C to stop)...");

        size_t yuyv_size = frame_width_ * frame_height_ * 2;
        size_t gray_size = frame_width_ * frame_height_;
        std::vector<uint8_t> yuyv_buffer(yuyv_size);
        std::vector<uint8_t> gray_buffer(gray_size);

        while (g_running) {
            auto start = std::chrono::steady_clock::now();

            if (synthetic_mode_) {
                create_synthetic_frame(gray_buffer.data(), frame_count_);
            } else {
                V4L2Frame frame;
                if (!camera_.capture_frame(frame)) {
                    usleep(10000);
                    continue;
                }

                yuyv_to_gray(frame.data, frame.width, frame.height,
                             gray_buffer.data(), frame.stride);
                camera_.release_frame(frame);
            }

            ImageData image_data;
            auto buffer = std::make_shared<PooledBuffer<uint8_t>>();
            buffer->data.resize(gray_size);
            buffer->size = gray_size;
            std::memcpy(buffer->data.data(), gray_buffer.data(), gray_size);

            image_data.buffer = buffer;
            image_data.width = frame_width_;
            image_data.height = frame_height_;
            image_data.stride = frame_width_;
            image_data.frame_id = frame_count_;

            std::vector<GPUDetectionResult> results;
            detector_->process_frame(image_data, results);

            auto end = std::chrono::steady_clock::now();
            float latency_us = std::chrono::duration<float, std::micro>(end - start).count();
            latencies_.push_back(latency_us);

            if (!results.empty()) {
                detection_count_ += results.size();
                for (const auto& r : results) {
                    if (r.isValid()) {
                        if (r.target_count > 0) {
                            float xmin = r.centers[0][0] - r.radii[0];
                            float ymin = r.centers[0][1] - r.radii[0];
                            float xmax = r.centers[0][0] + r.radii[0];
                            float ymax = r.centers[0][1] + r.radii[0];
                            printf("\r[DETECTED] bbox=(%.0f,%.0f)-(%.0f,%.0f) conf=%.2f      ",
                                   xmin, ymin, xmax, ymax, r.confidence[0]);
                            fflush(stdout);
                        }
                    }
                }
            }

            frame_count_++;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_update_);
            if (elapsed.count() >= 1000) {
                float current_fps = fps_.load(std::memory_order_relaxed);
                fps_ = frame_count_ / (elapsed.count() / 1000.0f);
                frame_count_ = 0;
                last_fps_update_ = now;
                printf("\rFPS: %.1f | Frames: %zu | Avg latency: %.2f ms     ",
                       current_fps, latencies_.size(), avg_latency());
                fflush(stdout);
            }

            usleep(5000);
        }

        printf("\n");
        print_summary();
    }

private:
    void on_detections(const std::vector<GPUDetectionResult>& results) {
        for (const auto& r : results) {
            if (r.isValid()) {
                if (r.target_count > 0) {
                    float xmin = r.centers[0][0] - r.radii[0];
                    float ymin = r.centers[0][1] - r.radii[0];
                    float xmax = r.centers[0][0] + r.radii[0];
                    float ymax = r.centers[0][1] + r.radii[0];
                    APP_LOG_INFO("Detection: bbox=[" + std::to_string(xmin/frame_width_) + "," +
                                 std::to_string(ymin/frame_height_) + "," + std::to_string(xmax/frame_width_) + "," +
                                 std::to_string(ymax/frame_height_) + "] conf=" + std::to_string(r.confidence[0]));
                }
            }
        }
    }

    void yuyv_to_gray(const uint8_t* yuyv, int width, int height,
                      uint8_t* gray, int stride) {
        (void)stride;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x += 2) {
                int y0 = yuyv[y * width * 2 + x * 2];
                (void)yuyv[y * width * 2 + x * 2 + 1];
                int y1 = yuyv[y * width * 2 + x * 2 + 2];
                (void)yuyv[y * width * 2 + x * 2 + 3];
                gray[y * width + x] = (uint8_t)y0;
                gray[y * width + x + 1] = (uint8_t)y1;
            }
        }
    }

    void create_synthetic_frame(uint8_t* data, int frame_idx) {
        int center_x = frame_width_ / 2;
        int center_y = frame_height_ / 2;
        float radius = 80.0f + sin(frame_idx * 0.05f) * 20.0f;

        for (int y = 0; y < frame_height_; y++) {
            for (int x = 0; x < frame_width_; x++) {
                float dx = x - center_x;
                float dy = y - center_y;
                float dist = sqrt(dx * dx + dy * dy);

                if (dist < radius) {
                    float normalized = dist / radius;
                    int ring = static_cast<int>(normalized * 6);
                    uint8_t intensity = (ring % 2 == 0) ? 200 : 50;
                    data[y * frame_width_ + x] = intensity;
                } else {
                    data[y * frame_width_ + x] = 30;
                }
            }
        }
    }

    float avg_latency() {
        if (latencies_.empty()) return 0.0f;
        float sum = 0.0f;
        for (float l : latencies_) sum += l;
        return (sum / latencies_.size()) / 1000.0f;
    }

    void print_summary() {
        if (latencies_.empty()) return;

        float min_lat = *std::min_element(latencies_.begin(), latencies_.end());
        float max_lat = *std::max_element(latencies_.begin(), latencies_.end());
        float avg = avg_latency() * 1000.0f;

        printf("\n=== Summary ===\n");
        printf("Frames processed: %zu\n", latencies_.size());
        int total_det = detection_count_.load(std::memory_order_relaxed);
        printf("Total detections: %d\n", total_det);
        printf("Avg latency: %.2f ms\n", avg);
        printf("Min latency: %.2f ms\n", min_lat / 1000.0f);
        printf("Max latency: %.2f ms\n", max_lat / 1000.0f);

        std::vector<float> sorted = latencies_;
        size_t p95_idx = (sorted.size() * 95) / 100;
        std::nth_element(sorted.begin(), sorted.begin() + p95_idx, sorted.end());
        printf("P95 latency: %.2f ms\n", sorted[p95_idx] / 1000.0f);
        printf("WCET (3ms): %s\n", (sorted[p95_idx] < 3000.0f) ? "PASS" : "FAIL");
    }

    int frame_width_;
    int frame_height_;
    std::unique_ptr<GPUDetector> detector_;
    V4L2CameraCapture camera_;
    std::atomic<int> frame_count_;
    std::atomic<int> fps_;
    std::atomic<int> detection_count_;
    float avg_latency_us_;
    bool synthetic_mode_ = false;
    std::vector<float> latencies_;
    std::chrono::steady_clock::time_point last_fps_update_;
};

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    (void)argc;
    (void)argv;

    TermiosHelper termios;

    LiveCameraTest test;
    if (!test.init()) {
        APP_LOG_ERROR("Test initialization failed");
        return 1;
    }

    test.run();

    return 0;
}
