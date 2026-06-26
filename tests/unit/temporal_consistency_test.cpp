/**
 * @file temporal_consistency_test.cpp
 * @brief Part 5: Temporal Consistency and Control-Loop Integrity Tests
 *
 * Tests:
 * - Sensor → compute → actuator phase alignment
 * - Long-run jitter accumulation
 * - Timebase monotonicity and rollover handling
 * - Timestamp discontinuities
 * - Defined behavior when deadlines are missed
 *
 * Distinguishes between:
 * - Tolerated timing violations
 * - Safety-triggering violations
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/safety_monitor.hpp"
#include "aurore/test_infrastructure.hpp"
#include "aurore/timing.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name)                                                          \
    do {                                                                        \
        g_tests_run.fetch_add(1);                                               \
        try {                                                                   \
            name();                                                             \
            g_tests_passed.fetch_add(1);                                        \
            std::cout << "  PASS: " << #name << std::endl;                      \
        } catch (const std::exception& e) {                                     \
            g_tests_failed.fetch_add(1);                                        \
            std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
        }                                                                       \
    } while (0)

#define ASSERT_TRUE(x)                                               \
    do {                                                             \
        if (!(x)) throw std::runtime_error("Assertion failed: " #x); \
    } while (0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b)                                                              \
    do {                                                                             \
        if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } while (0)
#define ASSERT_GT(a, b)                                                             \
    do {                                                                            \
        if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); \
    } while (0)
#define ASSERT_LT(a, b)                                                             \
    do {                                                                            \
        if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); \
    } while (0)
#define ASSERT_NE(a, b)                                                              \
    do {                                                                             \
        if ((a) == (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } while (0)
#define ASSERT_GE(a, b)                                                             \
    do {                                                                            \
        if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); \
    } while (0)
#define ASSERT_LE(a, b)                                                             \
    do {                                                                            \
        if ((a) > (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b); \
    } while (0)

constexpr uint64_t kFramePeriodNs = 8333333;
constexpr uint64_t k120HzPeriodNs = 8333333;

}  // anonymous namespace

// ============================================================================
// Timestamp Monotonicity Tests
// ============================================================================

TEST(test_timestamp_monotonic_raw) {
    std::vector<uint64_t> timestamps;

    for (int i = 0; i < 1000; i++) {
        timestamps.push_back(aurore::get_timestamp(aurore::ClockId::MonotonicRaw));
    }

    for (size_t i = 1; i < timestamps.size(); i++) {
        ASSERT_GE(timestamps[i], timestamps[i - 1]);
    }
}

TEST(test_timestamp_equality_allowed) {
    uint64_t ts1 = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    uint64_t ts2 = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);

    ASSERT_LE(ts1, ts2);
}

TEST(test_timestamp_safe) {
    int error;
    uint64_t ts = aurore::get_timestamp_safe(aurore::ClockId::MonotonicRaw, error);

    ASSERT_EQ(error, 0);
    ASSERT_GT(ts, 0);
}

// ============================================================================
// Timestamp Validator Tests
// ============================================================================

TEST(test_timestamp_validator_monotonic) {
    aurore::test::TimestampValidator validator(1000000);

    for (int i = 0; i < 100; i++) {
        validator.record_timestamp(static_cast<uint64_t>(1000000) +
                                   static_cast<uint64_t>(i) * 1000);
    }

    auto report = validator.validate();
    ASSERT_EQ(report.type, aurore::test::ViolationType::None);
}

TEST(test_timestamp_validator_detects_nonmonotonic) {
    aurore::test::TimestampValidator validator(1000000);

    validator.record_timestamp(1000);
    validator.record_timestamp(2000);
    validator.record_timestamp(1500);

    auto report = validator.validate();
    ASSERT_EQ(report.type, aurore::test::ViolationType::NonMonotonic);
    ASSERT_GT(report.total_violations, 0);
}

TEST(test_timestamp_validator_detects_discontinuity) {
    aurore::test::TimestampValidator validator(100000);

    validator.record_timestamp(1000);
    validator.record_timestamp(2000);
    validator.record_timestamp(2000000);

    auto report = validator.validate();
    ASSERT_EQ(report.type, aurore::test::ViolationType::Discontinuity);
}

TEST(test_timestamp_validator_reset) {
    aurore::test::TimestampValidator validator(100000);

    validator.record_timestamp(1000);
    validator.record_timestamp(2000);

    validator.reset();

    auto report = validator.validate();
    ASSERT_EQ(report.total_samples, 0);
}

// ============================================================================
// Phase Alignment Tests
// ============================================================================

TEST(test_phase_alignment_vision_to_track) {
    aurore::ThreadTiming vision_timing(k120HzPeriodNs, 0);
    aurore::ThreadTiming track_timing(k120HzPeriodNs, 2000000);

    auto vision_wakeup = vision_timing.next_wakeup_ns();
    auto track_wakeup = track_timing.next_wakeup_ns();

    int64_t phase_diff = static_cast<int64_t>(track_wakeup) - static_cast<int64_t>(vision_wakeup);

    ASSERT_GE(phase_diff, 1500000);
    ASSERT_LE(phase_diff, 2500000);
}

TEST(test_phase_alignment_vision_to_actuation) {
    aurore::ThreadTiming vision_timing(k120HzPeriodNs, 0);
    aurore::ThreadTiming actuation_timing(k120HzPeriodNs, 4000000);

    auto vision_wakeup = vision_timing.next_wakeup_ns();
    auto actuation_wakeup = actuation_timing.next_wakeup_ns();

    int64_t phase_diff =
        static_cast<int64_t>(actuation_wakeup) - static_cast<int64_t>(vision_wakeup);

    ASSERT_GE(phase_diff, 3500000);
    ASSERT_LE(phase_diff, 4500000);
}

TEST(test_phase_alignment_all_stages) {
    aurore::ThreadTiming vision(k120HzPeriodNs, 0);
    aurore::ThreadTiming track(k120HzPeriodNs, 2000000);
    aurore::ThreadTiming actuation(k120HzPeriodNs, 4000000);

    auto v = vision.next_wakeup_ns();
    auto t = track.next_wakeup_ns();
    auto a = actuation.next_wakeup_ns();

    ASSERT_LT(v, t);
    ASSERT_LT(t, a);
}

// ============================================================================
// Jitter Accumulation Tests
// ============================================================================

TEST(test_jitter_no_accumulation) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    std::vector<int64_t> jitters;
    std::atomic<uint64_t> deadline_misses(0);

    for (int i = 0; i < 100; i++) {
        bool on_time = timing.wait();
        if (!on_time) {
            deadline_misses.fetch_add(1, std::memory_order_relaxed);
        }

        auto now = aurore::get_timestamp();
        auto jitter = timing.calculate_jitter(now);
        jitters.push_back(jitter);
    }

    int64_t max_jitter = 0;
    int64_t min_jitter = 0;
    for (auto j : jitters) {
        max_jitter = std::max(max_jitter, j);
        min_jitter = std::min(min_jitter, j);
    }

    ASSERT_LT(max_jitter - min_jitter, 10000000);
}

TEST(test_jitter_within_bounds) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    std::vector<int64_t> jitters;

    for (int i = 0; i < 50; i++) {
        timing.wait();
        auto now = aurore::get_timestamp();
        jitters.push_back(std::abs(timing.calculate_jitter(now)));
    }

    int64_t max_jitter = 0;
    for (auto j : jitters) {
        max_jitter = std::max(max_jitter, j);
    }

    ASSERT_LT(max_jitter, 50000000);
}

// ============================================================================
// Deadline Miss Behavior Tests
// ============================================================================

TEST(test_deadline_miss_detection) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    timing.wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    bool on_time = timing.wait();

    ASSERT_FALSE(on_time);
    ASSERT_GT(timing.deadline_misses(), 0);
}

TEST(test_deadline_miss_counter) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    timing.wait();

    for (int i = 0; i < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.wait();
    }

    ASSERT_GE(timing.deadline_misses(), 3);
}

TEST(test_consecutive_misses_counter) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    timing.wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    timing.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    timing.wait();

    ASSERT_GE(timing.consecutive_misses(), 2);
}

TEST(test_consecutive_misses_reset) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    timing.wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    timing.wait();

    timing.wait();

    ASSERT_EQ(timing.consecutive_misses(), 0);
}

// ============================================================================
// Safety Monitor Timing Tests
// ============================================================================

TEST(test_safety_monitor_latency_tracking) {
    aurore::SafetyMonitorConfig config;
    config.vision_deadline_ns = 10000000;
    config.actuation_deadline_ns = 16666000;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    (void)aurore::get_timestamp();
    monitor.record_stage_latency(aurore::PipelineStage::VISION, 5000000);
    monitor.record_stage_latency(aurore::PipelineStage::TRACK, 3000000);
    monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 2000000);

    (void)monitor.get_stage_stats(aurore::PipelineStage::VISION);

    monitor.stop();
}

TEST(test_safety_monitor_stall_detection) {
    aurore::SafetyMonitorConfig config;
    config.per_stage.stall_threshold_ns = 5000000;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.record_stage_latency(aurore::PipelineStage::VISION, 20000000);

    const auto& stats = monitor.get_stage_stats(aurore::PipelineStage::VISION);

    ASSERT_TRUE(stats.is_stalled());

    monitor.stop();
}

TEST(test_safety_monitor_healthy_frame_count) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.record_stage_latency(aurore::PipelineStage::VISION, 1000000);
    monitor.record_stage_latency(aurore::PipelineStage::TRACK, 1000000);
    monitor.record_stage_latency(aurore::PipelineStage::ACTUATION, 1000000);

    monitor.record_frame_complete(3000000);

    (void)monitor.get_healthy_frames();
    auto total = monitor.get_total_frames();

    ASSERT_GE(total, 1);

    monitor.stop();
}

// ============================================================================
// Tolerated vs Safety-Triggering Violations
// ============================================================================

TEST(test_tolerated_violation_no_safety_trigger) {
    aurore::SafetyMonitorConfig config;
    config.max_consecutive_misses = 3;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    for (int i = 0; i < 2; i++) {
        [[maybe_unused]] bool rc = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

TEST(test_safety_triggering_violation) {
    aurore::SafetyMonitorConfig config;
    config.max_consecutive_misses = 2;
    config.vision_deadline_ns = 1000;  // 1µs — tight deadline so stale quickly

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Arm the watchdog: deliver one frame, then let it go stale
    monitor.update_vision_frame(1, aurore::get_timestamp(aurore::ClockId::MonotonicRaw));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));  // 5ms >> 2µs

    for (int i = 0; i < 5; i++) {
        [[maybe_unused]] bool rc = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_FALSE(safe);
}

TEST(test_vision_latency_within_tolerance) {
    aurore::SafetyMonitorConfig config;
    config.vision_deadline_ns = 20000000;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_vision_frame(1, 10000000);

    for (int i = 0; i < 10; i++) {
        [[maybe_unused]] bool rc = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

TEST(test_vision_latency_exceeds_tolerance) {
    aurore::SafetyMonitorConfig config;
    config.vision_deadline_ns = 1000;  // 1µs — tight deadline

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // update_vision_frame stores current time (ignores passed ts), so we sleep
    // after to let it go stale well beyond 2×deadline = 2µs
    monitor.update_vision_frame(1, 0 /* ignored */);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));  // 5ms >> 2µs

    for (int i = 0; i < 3; i++) {
        [[maybe_unused]] bool rc = monitor.run_cycle();
    }

    auto fault = monitor.current_fault();

    monitor.stop();

    ASSERT_NE(fault, aurore::SafetyFaultCode::NONE);
}

// ============================================================================
// Control Loop Integration Tests
// ============================================================================

TEST(test_control_loop_timing_stability) {
    aurore::ThreadTiming timing(kFramePeriodNs, 0);

    std::vector<uint64_t> cycle_times;
    std::atomic<bool> running(true);

    std::thread measurement([&]() {
        uint64_t last_time = aurore::get_timestamp();

        while (running.load(std::memory_order_acquire)) {
            timing.wait();

            uint64_t now = aurore::get_timestamp();
            uint64_t delta = now - last_time;
            cycle_times.push_back(delta);

            last_time = now;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running.store(false, std::memory_order_release);
    measurement.join();

    ASSERT_GT(cycle_times.size(), 40);

    uint64_t max_delta = 0;
    uint64_t min_delta = UINT64_MAX;
    for (auto dt : cycle_times) {
        max_delta = std::max(max_delta, dt);
        min_delta = std::min(min_delta, dt);
    }

    ASSERT_LT(max_delta - min_delta, 50000000);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 5: Temporal Consistency and Control-Loop Tests ===" << std::endl;
    std::cout << "Running temporal tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Timestamp Monotonicity ---" << std::endl;
    RUN_TEST(test_timestamp_monotonic_raw);
    RUN_TEST(test_timestamp_equality_allowed);
    RUN_TEST(test_timestamp_safe);

    std::cout << "\n--- Timestamp Validator ---" << std::endl;
    RUN_TEST(test_timestamp_validator_monotonic);
    RUN_TEST(test_timestamp_validator_detects_nonmonotonic);
    RUN_TEST(test_timestamp_validator_detects_discontinuity);
    RUN_TEST(test_timestamp_validator_reset);

    std::cout << "\n--- Phase Alignment ---" << std::endl;
    RUN_TEST(test_phase_alignment_vision_to_track);
    RUN_TEST(test_phase_alignment_vision_to_actuation);
    RUN_TEST(test_phase_alignment_all_stages);

    std::cout << "\n--- Jitter Accumulation ---" << std::endl;
    RUN_TEST(test_jitter_no_accumulation);
    RUN_TEST(test_jitter_within_bounds);

    std::cout << "\n--- Deadline Miss Behavior ---" << std::endl;
    RUN_TEST(test_deadline_miss_detection);
    RUN_TEST(test_deadline_miss_counter);
    RUN_TEST(test_consecutive_misses_counter);
    RUN_TEST(test_consecutive_misses_reset);

    std::cout << "\n--- Safety Monitor Timing ---" << std::endl;
    RUN_TEST(test_safety_monitor_latency_tracking);
    RUN_TEST(test_safety_monitor_stall_detection);
    RUN_TEST(test_safety_monitor_healthy_frame_count);

    std::cout << "\n--- Tolerated vs Safety-Triggering ---" << std::endl;
    RUN_TEST(test_tolerated_violation_no_safety_trigger);
    RUN_TEST(test_safety_triggering_violation);
    RUN_TEST(test_vision_latency_within_tolerance);
    RUN_TEST(test_vision_latency_exceeds_tolerance);

    std::cout << "\n--- Control Loop Integration ---" << std::endl;
    RUN_TEST(test_control_loop_timing_stability);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}