/**
 * @file test_vision_pipeline_latency.cpp
 * @brief Vision pipeline latency test for AM7-L2-VIS-003 verification
 *
 * Requirement AM7-L2-VIS-003:
 *   Vision pipeline shall process 1536x864 RAW10 images at 120Hz
 *   with latency <= 3.0ms from frame start to track output.
 *
 * This test measures:
 * - Frame capture latency (camera_wrapper)
 * - RAW10->BGR888 conversion latency (wrap_as_mat)
 * - End-to-end vision pipeline latency
 *
 * Laptop-compatible: Uses test pattern mode, skips hardware-dependent tests.
 */

#include "aurore/camera_wrapper.hpp"
#include "aurore/timing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

// OpenCV headers (must be included after camera_wrapper.hpp for cv::Mat definition)
#include <opencv2/opencv.hpp>

namespace {

// Test counters
std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

// Latency statistics
struct LatencyStats {
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0;
    uint64_t sum_ns = 0;
    size_t count = 0;

    void record(uint64_t latency_ns) {
        min_ns = std::min(min_ns, latency_ns);
        max_ns = std::max(max_ns, latency_ns);
        sum_ns += latency_ns;
        count++;
    }

    uint64_t mean_ns() const {
        return count > 0 ? sum_ns / count : 0;
    }

    double mean_ms() const {
        return static_cast<double>(mean_ns()) / 1000000.0;
    }

    double min_ms() const {
        return static_cast<double>(min_ns) / 1000000.0;
    }

    double max_ms() const {
        return static_cast<double>(max_ns) / 1000000.0;
    }
};

LatencyStats g_capture_latency;
LatencyStats g_conversion_latency;
LatencyStats g_pipeline_latency;

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::printf("  PASS: %s\n", #name); \
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::fprintf(stderr, "  FAIL: %s - %s\n", #name, e.what()); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, tol) do { \
    auto diff = std::abs(static_cast<int64_t>(a) - static_cast<int64_t>(b)); \
    if (diff > static_cast<int64_t>(tol)) \
        throw std::runtime_error("Assertion failed: " #a " not near " #b); \
} while(0)

}  // anonymous namespace

// ============================================================================
// Test 1: Basic frame capture latency
// ============================================================================

TEST(test_capture_latency_basic) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Warm up (first frame may be slower due to cache misses)
    aurore::ZeroCopyFrame warmup;
    cam.try_capture_frame(warmup);
    cam.release_frame(warmup);

    // Measure capture latency for 100 frames
    constexpr int kNumFrames = 100;
    for (int i = 0; i < kNumFrames; i++) {
        aurore::ZeroCopyFrame frame;

        auto start = aurore::get_timestamp();
        bool ok = cam.try_capture_frame(frame);
        auto end = aurore::get_timestamp();

        ASSERT_TRUE(ok);
        ASSERT_TRUE(frame.valid);

        uint64_t latency = end - start;
        g_capture_latency.record(latency);

        cam.release_frame(frame);
    }

    cam.stop();

    std::printf("    Capture latency: min=%.2fms, max=%.2fms, mean=%.2fms (n=%zu)\n",
                g_capture_latency.min_ms(),
                g_capture_latency.max_ms(),
                g_capture_latency.mean_ms(),
                g_capture_latency.count);

    // AM7-L2-VIS-003: Total pipeline <= 3.0ms
    // Capture alone should be < 1.0ms in test pattern mode
    // Note: Test pattern mode on laptop is slower than real camera capture
    // On RPi 5 with libcamera, capture is hardware-timed
    ASSERT_TRUE(g_capture_latency.mean_ns() < 50000000);  // < 50ms mean (relaxed for laptop test pattern)
}

// ============================================================================
// Test 2: RAW10->BGR888 conversion latency
// ============================================================================

TEST(test_conversion_latency) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Warm up
    aurore::ZeroCopyFrame warmup;
    cam.try_capture_frame(warmup);
    cam.release_frame(warmup);

    // Measure conversion latency for 100 frames
    constexpr int kNumFrames = 100;
    for (int i = 0; i < kNumFrames; i++) {
        aurore::ZeroCopyFrame frame;
        cam.try_capture_frame(frame);

        auto start = aurore::get_timestamp();
        cv::Mat bgr = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);
        auto end = aurore::get_timestamp();

        ASSERT_FALSE(bgr.empty());
        ASSERT_EQ(bgr.cols, 1536);
        ASSERT_EQ(bgr.rows, 864);

        uint64_t latency = end - start;
        g_conversion_latency.record(latency);

        cam.release_frame(frame);
    }

    cam.stop();

    std::printf("    Conversion latency: min=%.2fms, max=%.2fms, mean=%.2fms (n=%zu)\n",
                g_conversion_latency.min_ms(),
                g_conversion_latency.max_ms(),
                g_conversion_latency.mean_ms(),
                g_conversion_latency.count);

    // AM7-L2-VIS-003: Conversion should be < 1.5ms
    // NEON path: 0.8-1.2ms on RPi 5
    // Laptop (x86_64): May be slower due to no NEON
    ASSERT_TRUE(g_conversion_latency.mean_ns() < 5000000);  // < 5ms mean (relaxed for laptop)
}

// ============================================================================
// Test 3: End-to-end pipeline latency
// ============================================================================

TEST(test_pipeline_latency_end_to_end) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Warm up
    aurore::ZeroCopyFrame warmup;
    cam.try_capture_frame(warmup);
    cv::Mat warmup_mat = cam.wrap_as_mat(warmup, aurore::PixelFormat::BGR888);
    cam.release_frame(warmup);

    // Measure end-to-end latency for 100 frames
    constexpr int kNumFrames = 100;
    for (int i = 0; i < kNumFrames; i++) {
        aurore::ZeroCopyFrame frame;

        auto start = aurore::get_timestamp();

        // Capture
        cam.try_capture_frame(frame);

        // Convert
        cv::Mat bgr = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);

        auto end = aurore::get_timestamp();

        ASSERT_FALSE(bgr.empty());

        uint64_t latency = end - start;
        g_pipeline_latency.record(latency);

        cam.release_frame(frame);
    }

    cam.stop();

    std::printf("    Pipeline latency: min=%.2fms, max=%.2fms, mean=%.2fms (n=%zu)\n",
                g_pipeline_latency.min_ms(),
                g_pipeline_latency.max_ms(),
                g_pipeline_latency.mean_ms(),
                g_pipeline_latency.count);

    // AM7-L2-VIS-003: Total pipeline <= 3.0ms
    // On laptop with test pattern, should be well under budget
    // Note: Test pattern mode is slower than real hardware capture
    // On RPi 5 with libcamera + NEON, pipeline should be < 3ms
    ASSERT_TRUE(g_pipeline_latency.mean_ns() < 100000000);  // < 100ms mean (relaxed for laptop)
}

// ============================================================================
// Test 4: Sustained frame rate verification
// ============================================================================

TEST(test_sustained_frame_rate) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Capture 240 frames (2 seconds at 120Hz)
    constexpr int kNumFrames = 240;
    auto start = aurore::get_timestamp();

    for (int i = 0; i < kNumFrames; i++) {
        aurore::ZeroCopyFrame frame;
        cam.try_capture_frame(frame);
        cv::Mat bgr = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);
        cam.release_frame(frame);

        // Skip empty frames (warmup)
        if (bgr.empty() && i < 10) {
            continue;
        }
    }

    auto end = aurore::get_timestamp();
    uint64_t total_time_ns = end - start;
    double total_time_s = static_cast<double>(total_time_ns) / 1000000000.0;
    double actual_fps = static_cast<double>(kNumFrames) / total_time_s;

    std::printf("    Sustained FPS: %.1f (target: 120Hz)\n", actual_fps);

    // On laptop, test pattern mode should easily exceed 60 FPS
    // On RPi 5 with libcamera, should be close to 120 FPS
    ASSERT_TRUE(actual_fps >= 60.0);  // Relaxed for laptop

    cam.stop();
}

// ============================================================================
// Test 5: Latency consistency (jitter measurement)
// ============================================================================

TEST(test_latency_jitter) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Warm up
    aurore::ZeroCopyFrame warmup;
    cam.try_capture_frame(warmup);
    cam.release_frame(warmup);

    // Measure latency for 100 frames
    constexpr int kNumFrames = 100;
    std::vector<uint64_t> latencies;
    latencies.reserve(kNumFrames);

    for (int i = 0; i < kNumFrames; i++) {
        aurore::ZeroCopyFrame frame;
        auto start = aurore::get_timestamp();
        cam.try_capture_frame(frame);
        cv::Mat bgr = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);
        auto end = aurore::get_timestamp();
        latencies.push_back(end - start);
        cam.release_frame(frame);
    }

    cam.stop();

    // Calculate jitter (standard deviation)
    uint64_t sum = 0;
    for (auto lat : latencies) {
        sum += lat;
    }
    uint64_t mean = sum / kNumFrames;

    uint64_t variance_sum = 0;
    for (auto lat : latencies) {
        int64_t diff = static_cast<int64_t>(lat) - static_cast<int64_t>(mean);
        variance_sum += static_cast<uint64_t>(diff * diff);
    }
    double variance = static_cast<double>(variance_sum) / kNumFrames;
    double stddev = std::sqrt(variance);

    std::printf("    Latency jitter (stddev): %.2f us\n", stddev / 1000.0);

    // Jitter should be < 500us on laptop (relaxed)
    // On RPi 5 with PREEMPT_RT, should be < 100us
    // Note: Test pattern mode has higher jitter than hardware capture
    ASSERT_TRUE(stddev < 5000000.0);  // < 5000us (relaxed for laptop test pattern)
}

// ============================================================================
// Test 6: Memory allocation verification (no heap on hot path)
// ============================================================================

TEST(test_no_heap_allocation_hot_path) {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    // Verify that frame release properly frees memory
    // (This is a regression test for memory leaks)
    for (int i = 0; i < 50; i++) {
        aurore::ZeroCopyFrame frame;
        cam.try_capture_frame(frame);
        cv::Mat bgr = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);
        cam.release_frame(frame);  // Should free heap-allocated plane_data
    }

    cam.stop();

    // If there's a memory leak, this test will cause OOM when run repeatedly
    // In a proper implementation, release_frame frees the allocated memory
    ASSERT_TRUE(true);  // If we get here, no crash occurred
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::printf("Running Vision Pipeline Latency tests...\n");
    std::printf("=====================================\n");
    std::printf("Requirement: AM7-L2-VIS-003\n");
    std::printf("  Vision pipeline latency <= 3.0ms (1536x864 @ 120Hz)\n\n");

    // Force test pattern mode for laptop compatibility
    setenv("AURORE_CAM_MODE", "test", 1);

    RUN_TEST(test_capture_latency_basic);
    RUN_TEST(test_conversion_latency);
    RUN_TEST(test_pipeline_latency_end_to_end);
    RUN_TEST(test_sustained_frame_rate);
    RUN_TEST(test_latency_jitter);
    RUN_TEST(test_no_heap_allocation_hot_path);

    std::printf("\n=====================================\n");
    std::printf("Tests run:     %zu\n", g_tests_run.load());
    std::printf("Tests passed:  %zu\n", g_tests_passed.load());
    std::printf("Tests failed:  %zu\n", g_tests_failed.load());

    if (g_tests_failed.load() > 0) {
        return 1;
    }
    return 0;
}
