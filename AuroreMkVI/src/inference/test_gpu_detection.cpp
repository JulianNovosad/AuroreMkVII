#include "gpu_detector.h"
#include "util_logging.h"
#include <opencv2/opencv.hpp>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <cstdlib>

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

class GPUDetectionTest {
public:
    GPUDetectionTest()
        : frame_width_(1280)
        , frame_height_(720)
        , frame_count_(0)
        , fps_(0)
        , avg_latency_us_(0.0f)
    {
    }

    bool init() {
        Logger::init("gpu_test", "logs", nullptr);
        Logger::getInstance().start_writer_thread();

        APP_LOG_INFO("=== GPU Target Detection Test ===");

        detector_ = std::make_unique<GPUDetector>();

        if (!detector_->init(frame_width_, frame_height_)) {
            APP_LOG_ERROR("Failed to initialize GPU detector");
            return false;
        }

        if (!detector_->is_available()) {
            APP_LOG_WARNING("GPU detector not available, running in simulation mode");
            simulation_mode_ = true;
        }

        APP_LOG_INFO("GPU Detector initialized: " +
                     std::string(detector_->is_available() ? "Hardware" : "Simulation"));
        APP_LOG_INFO("Frame size: " + std::to_string(frame_width_) + "x" +
                     std::to_string(frame_height_));

        detector_->set_detection_callback([this](const std::vector<GPUDetectionResult>& results) {
            on_detections(results);
        });

        last_fps_update_ = std::chrono::steady_clock::now();
        return true;
    }

    void run_fps_test(int num_frames) {
        APP_LOG_INFO("Running FPS test for " + std::to_string(num_frames) + " frames...");

        std::vector<float> latencies;
        latencies.reserve(num_frames);

        std::vector<ImageData> synthetic_frames = create_synthetic_frames(num_frames);

        for (int i = 0; i < num_frames && g_running; i++) {
            auto start = std::chrono::steady_clock::now();

            std::vector<GPUDetectionResult> results;
            detector_->process_frame(synthetic_frames[i], results);

            auto end = std::chrono::steady_clock::now();
            float latency_us = std::chrono::duration<float, std::micro>(end - start).count();
            latencies.push_back(latency_us);

            frame_count_++;
            if (i % 30 == 0) {
                print_progress(i + 1, num_frames);
            }
        }

        print_results(latencies);
    }

    void run_live_test() {
        APP_LOG_INFO("Starting live detection test (Ctrl+C to stop)...");

        while (g_running) {
            std::vector<ImageData> synthetic_frames = create_synthetic_frames(1);
            std::vector<GPUDetectionResult> results;
            detector_->process_frame(synthetic_frames[0], results);

            if (!results.empty()) {
                for (const auto& r : results) {
                    if (r.isValid()) {
                        // Print the first detection's info
                        if (r.target_count > 0) {
                            float xmin = r.centers[0][0] - r.radii[0];
                            float ymin = r.centers[0][1] - r.radii[0];
                            float xmax = r.centers[0][0] + r.radii[0];
                            float ymax = r.centers[0][1] + r.radii[0];
                            printf("\r[TARGET] bbox=(%.3f,%.3f)-(%.3f,%.3f) conf=%.2f      ",
                                   xmin/frame_width_, ymin/frame_height_, xmax/frame_width_, ymax/frame_height_, r.confidence[0]);
                        }
                        fflush(stdout);
                    }
                }
            } else {
                int current_fps = fps_.load(std::memory_order_relaxed);
                printf("\rScanning... FPS: %d", current_fps);
                fflush(stdout);
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_update_);
            if (elapsed.count() >= 1000) {
                fps_.store(frame_count_ / (elapsed.count() / 1000.0f), std::memory_order_relaxed);
                frame_count_ = 0;
                last_fps_update_ = now;
            }

            usleep(10000);
        }

        printf("\n");
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

    std::vector<ImageData> create_synthetic_frames(int count) {
        std::vector<ImageData> frames;
        frames.reserve(count);

        size_t buffer_size = frame_width_ * frame_height_;
        auto buffer = std::make_shared<PooledBuffer<uint8_t>>();
        buffer->data.resize(buffer_size);
        buffer->size = buffer_size;

        for (int i = 0; i < count; i++) {
            ImageData frame;
            frame.buffer = buffer;
            frame.width = frame_width_;
            frame.height = frame_height_;
            frame.stride = frame_width_;
            frame.frame_id = i;

            uint8_t* data = buffer->data.data();
            create_target_pattern(data, i);

            frames.push_back(frame);
        }

        return frames;
    }

    void create_target_pattern(uint8_t* data, int frame_idx) {
        int center_x = frame_width_ / 2;
        int center_y = frame_height_ / 2;
        float radius = 80.0f + sin(frame_idx * 0.1f) * 10.0f;

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

    void print_progress(int current, int total) {
        float pct = (current * 100.0f) / total;
        printf("\rProgress: %d/%d frames (%.1f%%)", current, total, pct);
        fflush(stdout);
    }

    void print_results(const std::vector<float>& latencies) {
        if (latencies.empty()) {
            APP_LOG_ERROR("No latencies recorded");
            return;
        }

        float min_lat = *std::min_element(latencies.begin(), latencies.end());
        float max_lat = *std::max_element(latencies.begin(), latencies.end());
        float sum = 0.0f;
        for (float l : latencies) sum += l;
        float avg_lat = sum / latencies.size();

        float fps = 1000000.0f / avg_lat;

        std::vector<float> sorted_lat = latencies;
        size_t p95_idx = (sorted_lat.size() * 95) / 100;
        std::nth_element(sorted_lat.begin(), sorted_lat.begin() + p95_idx, sorted_lat.end());
        float p95_lat = sorted_lat[p95_idx];

        printf("\n=== FPS Test Results ===\n");
        printf("Frames processed: %zu\n", latencies.size());
        printf("Average FPS: %.2f\n", fps);
        printf("Latency (avg): %.2f us (%.3f ms)\n", avg_lat, avg_lat / 1000.0f);
        printf("Latency (min): %.2f us\n", min_lat);
        printf("Latency (max): %.2f us\n", max_lat);
        printf("Latency (P95): %.2f us\n", p95_lat);
        printf("WCET check: %.2f ms < 3.0 ms? %s\n",
               p95_lat / 1000.0f, (p95_lat < 3000.0f) ? "PASS" : "FAIL");

        APP_LOG_INFO("FPS Test Results: " + std::to_string(fps) + " fps, " +
                     std::to_string(avg_lat) + " us avg latency");
    }

    int frame_width_;
    int frame_height_;
    std::unique_ptr<GPUDetector> detector_;
    std::atomic<int> frame_count_;
    std::atomic<int> fps_;
    float avg_latency_us_;
    bool simulation_mode_ = false;
    std::chrono::steady_clock::time_point last_fps_update_;
};

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int test_frames = 100;
    bool live_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            test_frames = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--live") == 0) {
            live_mode = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("GPU Target Detection Test\n");
            printf("Usage: %s [options]\n", argv[0]);
            printf("  --frames N   Run N frames for FPS test (default: 100)\n");
            printf("  --live       Run live detection mode\n");
            printf("  --help       Show this help\n");
            return 0;
        }
    }

    TermiosHelper termios;

    GPUDetectionTest test;
    if (!test.init()) {
        APP_LOG_ERROR("Test initialization failed");
        return 1;
    }

    if (live_mode) {
        test.run_live_test();
    } else {
        test.run_fps_test(test_frames);
    }

    return 0;
}
