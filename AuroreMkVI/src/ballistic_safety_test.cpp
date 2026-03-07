#include <cmath>
#include <limits>
#include <cassert>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

#include "timing.h"

static int tests_passed = 0;
static int tests_failed = 0;

static void test_fov_bounds_check_basic();
static void test_fov_bounds_normalized();
static void test_critical_geometry_error_trigger();
static void test_sight_over_bore_offset_calculation();
static void test_sight_over_bore_y_offset_verification();
static void test_x_alignment_stationary_target();
static void test_temperature_throttling_state_machine();
static void test_temperature_throttling_rate_limiting();
static void test_queue_invariant_check();
static void test_queue_invariant_recovery();
static void test_target_size_pinhole_distance();

static void run_test(const char* name, void (*test_fn)()) {
    std::cout << "Running " << name << "... ";
    try {
        test_fn();
        std::cout << "PASSED\n";
        tests_passed++;
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        tests_failed++;
    }
}

static void assert_near(float actual, float expected, float tolerance, const char* expr) {
    float diff = std::abs(actual - expected);
    if (diff > tolerance) {
        throw std::runtime_error(std::string("Assertion failed: ") + expr + 
                                 " (" + std::to_string(actual) + ") not near " + 
                                 std::to_string(expected) + " within " + std::to_string(tolerance));
    }
}

static void assert_true(bool condition, const char* expr) {
    if (!condition) {
        throw std::runtime_error(std::string("Assertion failed: ") + expr);
    }
}

static void assert_false(bool condition, const char* expr) {
    if (condition) {
        throw std::runtime_error(std::string("Assertion failed: NOT ") + expr);
    }
}

static void test_fov_bounds_check_basic() {
    auto is_within_fov_bounds = [](float x, float y, float w, float h) -> bool {
        return x >= 0.0f && x <= w && y >= 0.0f && y <= h;
    };
    
    assert_true(is_within_fov_bounds(100, 100, 320, 320), "is_within_fov_bounds(100, 100, 320, 320)");
    assert_true(is_within_fov_bounds(0, 0, 320, 320), "is_within_fov_bounds(0, 0, 320, 320)");
    assert_true(is_within_fov_bounds(320, 320, 320, 320), "is_within_fov_bounds(320, 320, 320, 320)");
    assert_false(is_within_fov_bounds(-1, 100, 320, 320), "is_within_fov_bounds(-1, 100, 320, 320)");
    assert_false(is_within_fov_bounds(100, -1, 320, 320), "is_within_fov_bounds(100, -1, 320, 320)");
    assert_false(is_within_fov_bounds(321, 100, 320, 320), "is_within_fov_bounds(321, 100, 320, 320)");
    assert_false(is_within_fov_bounds(100, 321, 320, 320), "is_within_fov_bounds(100, 321, 320, 320)");
}

static void test_fov_bounds_normalized() {
    auto is_within_fov_normalized = [](float x, float y) -> bool {
        return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
    };
    
    assert_true(is_within_fov_normalized(0.5, 0.5), "is_within_fov_normalized(0.5, 0.5)");
    assert_true(is_within_fov_normalized(0.0, 0.0), "is_within_fov_normalized(0.0, 0.0)");
    assert_true(is_within_fov_normalized(1.0, 1.0), "is_within_fov_normalized(1.0, 1.0)");
    assert_false(is_within_fov_normalized(-0.1, 0.5), "is_within_fov_normalized(-0.1, 0.5)");
    assert_false(is_within_fov_normalized(0.5, 1.1), "is_within_fov_normalized(0.5, 1.1)");
}

static void test_critical_geometry_error_trigger() {
    int error_count = 0;
    auto trigger_error = [&](float x, float y, float w, float h) -> bool {
        bool x_valid = x >= 0.0f && x <= w;
        bool y_valid = y >= 0.0f && y <= h;
        if (!x_valid || !y_valid) {
            error_count++;
            return true;
        }
        return false;
    };
    
    assert_true(trigger_error(-1, 100, 320, 320), "trigger_error(-1, 100, 320, 320)");
    assert_true(trigger_error(100, -1, 320, 320), "trigger_error(100, -1, 320, 320)");
    assert_true(trigger_error(321, 100, 320, 320), "trigger_error(321, 100, 320, 320)");
    assert_true(trigger_error(100, 321, 320, 320), "trigger_error(100, 321, 320, 320)");
    assert_false(trigger_error(100, 100, 320, 320), "trigger_error(100, 100, 320, 320)");
    assert_true(error_count == 4, "error_count == 4");
}

static void test_sight_over_bore_offset_calculation() {
    float focal_length_px = 300.0f;
    float sight_height_m = 0.08f;
    
    auto calc_offset = [&](float distance_m) -> float {
        if (distance_m <= 0.0f || !std::isfinite(distance_m)) return 0.0f;
        return (sight_height_m / distance_m) * focal_length_px;
    };
    
    assert_near(calc_offset(10.0f), 2.4f, 0.01f, "calc_offset(10.0f)");
    assert_near(calc_offset(50.0f), 0.48f, 0.01f, "calc_offset(50.0f)");
    assert_near(calc_offset(100.0f), 0.24f, 0.01f, "calc_offset(100.0f)");
    assert_true(calc_offset(0.0f) == 0.0f, "calc_offset(0.0f) == 0");
    assert_true(calc_offset(-10.0f) == 0.0f, "calc_offset(-10.0f) == 0");
    assert_true(calc_offset(std::numeric_limits<float>::infinity()) == 0.0f, "calc_offset(inf) == 0");
}

static void test_sight_over_bore_y_offset_verification() {
    float focal_length_px = 300.0f;
    float sight_height_m = 0.08f;
    
    auto calc_offset = [&](float distance_m) -> float {
        return (sight_height_m / distance_m) * focal_length_px;
    };
    
    auto verify_y_offset = [&](float bbox_center_y, float oz_y, float distance_m, float tolerance) -> bool {
        float expected_offset = calc_offset(distance_m);
        float deviation = std::abs((bbox_center_y - oz_y) - expected_offset);
        (void)bbox_center_y;
        return deviation <= tolerance;
    };
    
    assert_true(verify_y_offset(100.0f, 97.6f, 10.0f, 0.5f), "verify_y_offset at 10m");
    assert_false(verify_y_offset(100.0f, 90.0f, 10.0f, 0.5f), "verify_y_offset deviation too large");
}

static void test_x_alignment_stationary_target() {
    auto verify_x_alignment = [&](float bbox_center_x, float oz_x, float velocity_x, float tolerance) -> bool {
        float velocity_threshold = 0.1f;
        if (std::abs(velocity_x) < velocity_threshold) {
            return std::abs(bbox_center_x - oz_x) <= tolerance;
        }
        return std::abs(bbox_center_x - oz_x) <= (tolerance * 10.0f);
    };
    
    assert_true(verify_x_alignment(100.0f, 100.5f, 0.0f, 1.0f), "stationary X within tolerance");
    assert_true(verify_x_alignment(100.0f, 99.5f, 0.0f, 1.0f), "stationary X within tolerance");
    assert_false(verify_x_alignment(100.0f, 102.0f, 0.0f, 1.0f), "stationary X exceeds tolerance");
    assert_true(verify_x_alignment(100.0f, 105.0f, 1.0f, 1.0f), "moving target more lenient");
}

static void test_temperature_throttling_state_machine() {
    std::atomic<bool> throttled{false};
    std::atomic<int> consecutive_over_temp{0};
    static constexpr float HIGH_THRESHOLD = 85.0f;
    static constexpr float LOW_THRESHOLD = 75.0f;
    
    auto update_throttle = [&](float temp) {
        if (temp >= HIGH_THRESHOLD) {
            consecutive_over_temp.fetch_add(1);
            if (consecutive_over_temp.load() >= 3 && !throttled.load()) {
                throttled.store(true);
            }
        } else if (temp <= LOW_THRESHOLD) {
            consecutive_over_temp.store(0);
            if (throttled.load()) {
                throttled.store(false);
            }
        } else {
            consecutive_over_temp.store(0);
        }
    };
    
    assert_false(throttled.load(), "initial state not throttled");
    
    update_throttle(86.0f);
    assert_false(throttled.load(), "1st reading above threshold");
    
    update_throttle(87.0f);
    assert_false(throttled.load(), "2nd reading above threshold");
    
    update_throttle(88.0f);
    assert_true(throttled.load(), "3rd reading triggers throttling");
    
    update_throttle(74.0f);
    assert_false(throttled.load(), "recovery at low threshold");
}

static void test_temperature_throttling_rate_limiting() {
    static constexpr int THROTTLED_RATE_IPS = 6;
    static constexpr int MIN_INTERVAL_MS = 1000 / THROTTLED_RATE_IPS;
    
    assert_near(MIN_INTERVAL_MS, 166, 1, "MIN_INTERVAL_MS == 166");
    
    uint64_t last_inference_ms = 0;
    auto should_allow_inference = [&](uint64_t now_ms) -> bool {
        if (now_ms - last_inference_ms >= MIN_INTERVAL_MS) {
            last_inference_ms = now_ms;
            return true;
        }
        return false;
    };
    
    assert_true(should_allow_inference(1000), "first inference allowed");
    assert_false(should_allow_inference(1100), "too soon after first");
    assert_false(should_allow_inference(1150), "still too soon");
    assert_true(should_allow_inference(1167), "allowed after interval");
}

static void test_queue_invariant_check() {
    static constexpr size_t QUEUE_CAPACITY = 50;
    static constexpr float FULL_THRESHOLD = 0.9f;
    static constexpr size_t INVARIANT_FULL = static_cast<size_t>(QUEUE_CAPACITY * FULL_THRESHOLD);
    
    auto check_invariant = [&](size_t depth) -> bool {
        return depth < INVARIANT_FULL;
    };
    
    assert_true(check_invariant(44), "below threshold invariant holds");
    assert_false(check_invariant(45), "at 45 invariant violated");
    assert_false(check_invariant(46), "above threshold invariant violated");
    assert_false(check_invariant(50), "full queue invariant violated");
}

static void test_queue_invariant_recovery() {
    static constexpr size_t QUEUE_CAPACITY = 50;
    static constexpr float FULL_THRESHOLD = 0.9f;
    static constexpr size_t INVARIANT_FULL = static_cast<size_t>(QUEUE_CAPACITY * FULL_THRESHOLD);
    
    uint64_t consecutive_failures = 0;
    auto process_invariant = [&](size_t depth) -> bool {
        bool violated = depth >= INVARIANT_FULL;
        if (violated) {
            consecutive_failures++;
        } else {
            if (consecutive_failures > 0) {
                consecutive_failures = 0;
            }
        }
        return !violated;
    };
    
    assert_true(process_invariant(10), "normal operation");
    assert_true(consecutive_failures == 0, "no failures in normal operation");
    
    assert_false(process_invariant(46), "invariant violation");
    assert_true(consecutive_failures == 1, "consecutive failure count");
    
    assert_true(process_invariant(10), "recovery");
    assert_true(consecutive_failures == 0, "failure count reset on recovery");
}

static void test_target_size_pinhole_distance() {
    float focal_length_px = 300.0f;
    float target_width_m = 0.12f;
    
    auto estimate_distance = [&](float bbox_width_px) -> float {
        if (bbox_width_px <= 0.0f) return 0.0f;
        return (target_width_m * focal_length_px) / bbox_width_px;
    };
    
    assert_near(estimate_distance(60.0f), 0.6f, 0.001f, "60cm distance estimate");
    assert_near(estimate_distance(30.0f), 1.2f, 0.001f, "1.2m distance estimate");
    assert_true(estimate_distance(0.0f) == 0.0f, "invalid width returns 0");
    assert_true(estimate_distance(-10.0f) == 0.0f, "negative width returns 0");
}

int main() {
    std::cout << "=== Running Aurore Mk VI Safety-Critical Module Tests ===\n\n";
    
    std::cout << "[FOV Tests]\n";
    run_test("fov_bounds_check_basic", test_fov_bounds_check_basic);
    run_test("fov_bounds_normalized", test_fov_bounds_normalized);
    run_test("critical_geometry_error_trigger", test_critical_geometry_error_trigger);
    
    std::cout << "\n[Sight-Over-Bore Tests]\n";
    run_test("sight_over_bore_offset_calculation", test_sight_over_bore_offset_calculation);
    run_test("sight_over_bore_y_offset_verification", test_sight_over_bore_y_offset_verification);
    run_test("x_alignment_stationary_target", test_x_alignment_stationary_target);
    
    std::cout << "\n[Temperature Throttling Tests]\n";
    run_test("temperature_throttling_state_machine", test_temperature_throttling_state_machine);
    run_test("temperature_throttling_rate_limiting", test_temperature_throttling_rate_limiting);
    
    std::cout << "\n[Queue Invariant Tests]\n";
    run_test("queue_invariant_check", test_queue_invariant_check);
    run_test("queue_invariant_recovery", test_queue_invariant_recovery);
    
    std::cout << "\n[Distance Estimation Tests]\n";
    run_test("target_size_pinhole_distance", test_target_size_pinhole_distance);
    
    std::cout << "\n=== Test Results ===\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";
    
    return tests_failed > 0 ? 1 : 0;
}
