/**
 * @file camera_wrapper_test.cpp
 * @brief Unit tests for CameraWrapper (test pattern mode, no hardware required)
 *
 * Runs entirely in test pattern mode (no libcamera hardware, no webcam).
 * AURORE_CAM_MODE is not set so the implementation defaults to test pattern.
 */

#include "aurore/camera_wrapper.hpp"

#include <cassert>
#include <cstdio>
#include <stdexcept>

#include <opencv2/opencv.hpp>

// ---------------------------------------------------------------------------
// Minimal test harness (no external framework)
// ---------------------------------------------------------------------------

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(expr)                                                         \
    do {                                                                    \
        ++g_tests_run;                                                      \
        if (!(expr)) {                                                      \
            std::fprintf(stderr, "  FAIL [%s:%d]: %s\n",                   \
                         __FILE__, __LINE__, #expr);                        \
            ++g_tests_failed;                                               \
        } else {                                                            \
            ++g_tests_passed;                                               \
        }                                                                   \
    } while (0)

[[maybe_unused]] static void run_test(const char* name, void (*fn)()) {
    std::printf("[ RUN  ] %s\n", name);
    fn();
    std::printf("[ %s ] %s\n", g_tests_failed == 0 ? " OK " : "FAIL", name);
}

// ---------------------------------------------------------------------------
// Helper: free heap-allocated plane data that capture_frame_stub allocates.
// The implementation stores error[0] = 1 to signal that plane_data[0] must
// be deleted with delete[].
// ---------------------------------------------------------------------------
static void free_frame(aurore::ZeroCopyFrame& frame) {
    if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
        delete[] static_cast<uint8_t*>(frame.plane_data[0]);
        frame.plane_data[0] = nullptr;
        frame.error[0] = 0;
    }
}

// ---------------------------------------------------------------------------
// Test 1: default construction
// ---------------------------------------------------------------------------
static void test_default_construction() {
    aurore::CameraWrapper cam;  // default config (1536x864, 120fps)
    CHECK(!cam.is_running());
    CHECK(cam.frame_count() == 0);
    CHECK(cam.error_count() == 0);
}

// ---------------------------------------------------------------------------
// Test 2: invalid config throws CameraException
// ---------------------------------------------------------------------------
static void test_invalid_config_throws() {
    aurore::CameraConfig bad_cfg;
    bad_cfg.width = 0;  // invalid

    bool threw = false;
    try {
        aurore::CameraWrapper cam(bad_cfg);
        (void)cam;
    } catch (const aurore::CameraException&) {
        threw = true;
    } catch (...) {
        // Wrong exception type
    }
    CHECK(threw);
}

// ---------------------------------------------------------------------------
// Test 3: init / start / stop lifecycle
// ---------------------------------------------------------------------------
static void test_init_and_start_stop_lifecycle() {
    aurore::CameraWrapper cam;

    bool inited = cam.init();
    CHECK(inited);

    bool started = cam.start();
    CHECK(started);
    CHECK(cam.is_running());

    cam.stop();
    CHECK(!cam.is_running());
}

// ---------------------------------------------------------------------------
// Test 4: try_capture_frame returns valid test-pattern frame
// ---------------------------------------------------------------------------
static void test_try_capture_returns_valid_test_pattern() {
    aurore::CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    cfg.fps    = 120;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    aurore::ZeroCopyFrame frame;
    bool ok = cam.try_capture_frame(frame);

    CHECK(ok);
    CHECK(frame.valid == true);
    CHECK(frame.width  == 1536);
    CHECK(frame.height == 864);
    CHECK(frame.plane_data[0] != nullptr);

    free_frame(frame);
}

// ---------------------------------------------------------------------------
// Test 5: wrap_as_mat returns a non-empty BGR Mat
// ---------------------------------------------------------------------------
static void test_wrap_as_mat_returns_bgr_frame() {
    aurore::CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    cfg.fps    = 30;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    aurore::ZeroCopyFrame frame;
    bool ok = cam.try_capture_frame(frame);
    CHECK(ok);

    cv::Mat mat = cam.wrap_as_mat(frame, aurore::PixelFormat::BGR888);
    CHECK(!mat.empty());
    CHECK(mat.cols == 320);
    CHECK(mat.rows == 240);
    CHECK(mat.channels() == 3);

    free_frame(frame);
}

// ---------------------------------------------------------------------------
// Test 6: sequence numbers increment between captures
// ---------------------------------------------------------------------------
static void test_sequence_numbers_increment() {
    aurore::CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    cfg.fps    = 30;

    aurore::CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    aurore::ZeroCopyFrame f1, f2;

    bool ok1 = cam.try_capture_frame(f1);
    CHECK(ok1);

    bool ok2 = cam.try_capture_frame(f2);
    CHECK(ok2);

    CHECK(f2.sequence > f1.sequence);

    free_frame(f1);
    free_frame(f2);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    // Ensure test pattern mode regardless of environment
    setenv("AURORE_CAM_MODE", "test", /*overwrite=*/1);

    // Track per-test failures by resetting counters around each test.
    // We use a simple approach: record total failures before and after.

    int prev_failed = 0;

    auto run = [&](const char* name, void (*fn)()) {
        prev_failed = g_tests_failed;
        std::printf("[ RUN  ] %s\n", name);
        fn();
        if (g_tests_failed == prev_failed) {
            std::printf("[  OK  ] %s\n", name);
        } else {
            std::printf("[ FAIL ] %s\n", name);
        }
    };

    run("test_default_construction",                  test_default_construction);
    run("test_invalid_config_throws",                 test_invalid_config_throws);
    run("test_init_and_start_stop_lifecycle",          test_init_and_start_stop_lifecycle);
    run("test_try_capture_returns_valid_test_pattern", test_try_capture_returns_valid_test_pattern);
    run("test_wrap_as_mat_returns_bgr_frame",          test_wrap_as_mat_returns_bgr_frame);
    run("test_sequence_numbers_increment",             test_sequence_numbers_increment);

    std::printf("\nResults: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        std::printf(", %d FAILED\n", g_tests_failed);
        return 1;
    }
    std::printf(" - ALL PASS\n");
    return 0;
}
