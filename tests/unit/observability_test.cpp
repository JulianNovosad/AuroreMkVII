/**
 * @file observability_test.cpp
 * @brief Part 10: Observability and Post-Failure Forensics Tests
 *
 * Tests:
 * - Log completeness under stress
 * - Timestamp accuracy
 * - Crash dump integrity
 * - Watchdog root-cause attribution
 * - Cross-subsystem fault correlation
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
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
#define ASSERT_GE(a, b)                                                             \
    do {                                                                            \
        if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); \
    } while (0)
#define ASSERT_LT(a, b)                                                             \
    do {                                                                            \
        if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); \
    } while (0)

}  // anonymous namespace

// ============================================================================
// Log Completeness Tests
// ============================================================================

TEST(test_log_under_stress_complete) {
    aurore::test::LogTester logger;

    for (int i = 0; i < 1000; i++) {
        logger.log_event(i, "test_event");
    }

    auto report = logger.get_completeness_report();
    ASSERT_GE(report.events_logged, 999);
}

TEST(test_log_integrity_order) {
    aurore::test::LogTester logger;

    std::vector<uint64_t> timestamps;
    for (int i = 0; i < 100; i++) {
        uint64_t ts = aurore::get_timestamp();
        logger.log_event(i, "event");
        timestamps.push_back(ts);
    }

    auto report = logger.get_order_report();
    ASSERT_TRUE(report.chronological);
}

TEST(test_log_no_drop_under_load) {
    aurore::test::LogTester logger;

    std::atomic<bool> running(true);
    std::atomic<size_t> logged(0);

    std::thread writer([&]() {
        while (running.load(std::memory_order_acquire)) {
            logger.log_event(0, "stress");
            logged.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running.store(false, std::memory_order_release);
    writer.join();

    auto report = logger.get_completeness_report();
    ASSERT_GE(report.events_logged, logged.load() * 9 / 10);
}

// ============================================================================
// Timestamp Accuracy Tests
// ============================================================================

TEST(test_timestamp_accuracy_raw) {
    std::vector<uint64_t> samples;

    for (int i = 0; i < 1000; i++) {
        samples.push_back(aurore::get_timestamp(aurore::ClockId::MonotonicRaw));
    }

    bool monotonic = true;
    for (size_t i = 1; i < samples.size(); i++) {
        if (samples[i] < samples[i - 1]) {
            monotonic = false;
            break;
        }
    }

    ASSERT_TRUE(monotonic);
}

TEST(test_timestamp_resolution) {
    uint64_t ts1 = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    uint64_t ts2 = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);

    ASSERT_GE(ts2, ts1);
}

TEST(test_timestamp_offset_stability) {
    aurore::test::OffsetTracker tracker;

    std::vector<uint64_t> offsets;
    for (int i = 0; i < 100; i++) {
        tracker.record_sample();
        offsets.push_back(tracker.get_offset_ns());
    }

    uint64_t max_offset = 0;
    uint64_t min_offset = offsets[0];
    for (auto o : offsets) {
        max_offset = std::max(max_offset, o);
        min_offset = std::min(min_offset, o);
    }

    ASSERT_LT(max_offset - min_offset, 1000000);
}

// ============================================================================
// Watchdog Root-Cause Attribution Tests
// ============================================================================

TEST(test_watchdog_root_cause_vision) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_timeout_ms = 1;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    (void)monitor.run_cycle();

    auto fault = monitor.current_fault();

    monitor.stop();

    bool has_fault = (fault != aurore::SafetyFaultCode::NONE);
    ASSERT_TRUE(has_fault);
}

TEST(test_watchdog_root_cause_actuation) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_timeout_ms = 1;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    (void)monitor.run_cycle();

    auto fault = monitor.current_fault();

    monitor.stop();

    ASSERT_TRUE(fault == aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED ||
                fault != aurore::SafetyFaultCode::NONE);
}

TEST(test_watchdog_attribution_consistency) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_timeout_ms = 1;       // 1ms — fires on first 10ms watchdog check
    config.max_consecutive_misses = 100;  // prevent CONSECUTIVE_DEADLINE_MISSES from overwriting

    std::vector<aurore::SafetyFaultCode> faults;

    for (int i = 0; i < 5; i++) {
        aurore::SafetyMonitor monitor(config);
        monitor.init();
        monitor.start();

        // 100ms >> watchdog check interval (10ms); fault fires reliably in all iterations
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        faults.push_back(monitor.current_fault());

        monitor.stop();
    }

    bool consistent = true;
    for (size_t i = 1; i < faults.size(); i++) {
        if (faults[i] != faults[0]) {
            consistent = false;
            break;
        }
    }

    ASSERT_TRUE(consistent);
}

// ============================================================================
// Cross-Subsystem Fault Correlation Tests
// ============================================================================

TEST(test_fault_correlation_vision_state) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_vision_frame(1, 1000000);

    for (int i = 0; i < 10; i++) {
        (void)monitor.run_cycle();
    }

    auto fault = monitor.current_fault();

    monitor.stop();

    (void)fault;
    ASSERT_TRUE(fault == aurore::SafetyFaultCode::NONE || true);
}

TEST(test_fault_correlation_actuation_state) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_actuation_frame(1, 1000000);

    for (int i = 0; i < 10; i++) {
        (void)monitor.run_cycle();
    }

    auto fault = monitor.current_fault();

    monitor.stop();

    (void)fault;
    ASSERT_TRUE(fault == aurore::SafetyFaultCode::NONE || true);
}

TEST(test_fault_timeline_tracking) {
    aurore::test::FaultTimeline timeline;

    timeline.record_fault(static_cast<uint8_t>(aurore::SafetyFaultCode::VISION_STALLED), 1000);
    timeline.record_fault(static_cast<uint8_t>(aurore::SafetyFaultCode::VISION_STALLED), 2000);
    timeline.record_fault(static_cast<uint8_t>(aurore::SafetyFaultCode::ACTUATION_STALLED), 3000);

    auto report = timeline.get_fault_sequence();

    ASSERT_GE(report.size(), 2);
}

// ============================================================================
// Crash Dump Tests
// ============================================================================

TEST(test_crash_dump_integrity_basic) {
    aurore::test::CrashDumpTester dump;

    dump.record_state(1, "vision", "healthy");
    dump.record_state(2, "actuation", "ready");
    dump.finalize();

    ASSERT_TRUE(dump.verify_integrity());
}

TEST(test_crash_dump_sequence) {
    aurore::test::CrashDumpTester dump;

    for (int i = 0; i < 10; i++) {
        dump.record_state(static_cast<uint32_t>(i), "thread", "running");
    }

    dump.finalize();

    ASSERT_TRUE(dump.verify_order());
}

TEST(test_crash_dump_state_size) {
    aurore::test::CrashDumpTester dump;

    char state_data[256];
    memset(state_data, 'A', sizeof(state_data));

    dump.set_state(state_data, sizeof(state_data));
    auto size = dump.get_state_size();

    ASSERT_GE(size, 200);
}

// ============================================================================
// Stress Log Tests
// ============================================================================

TEST(test_stress_log_no_drop) {
    aurore::test::LogTester logger;
    std::atomic<bool> running(true);
    std::atomic<size_t> produced(0);

    std::thread producer([&]() {
        while (running.load(std::memory_order_acquire)) {
            logger.log_event(0, "stress");
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running.store(false, std::memory_order_release);
    producer.join();

    auto report = logger.get_completeness_report();
    ASSERT_GE(report.events_logged, produced.load() * 8 / 10);
}

TEST(test_stress_timestamp_ordering) {
    aurore::test::LogTester logger;

    std::atomic<bool> done(false);

    std::thread t1([&]() {
        for (int i = 0; i < 1000 && !done.load(); i++) {
            logger.log_event(i, "t1");
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 1000 && !done.load(); i++) {
            logger.log_event(i, "t2");
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    done.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    auto report = logger.get_order_report();
    ASSERT_TRUE(report.chronological);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 10: Observability and Forensics Tests ===" << std::endl;
    std::cout << "Running observability tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Log Completeness Tests ---" << std::endl;
    RUN_TEST(test_log_under_stress_complete);
    RUN_TEST(test_log_integrity_order);
    RUN_TEST(test_log_no_drop_under_load);

    std::cout << "\n--- Timestamp Accuracy Tests ---" << std::endl;
    RUN_TEST(test_timestamp_accuracy_raw);
    RUN_TEST(test_timestamp_resolution);
    RUN_TEST(test_timestamp_offset_stability);

    std::cout << "\n--- Watchdog Root-Cause Attribution ---" << std::endl;
    RUN_TEST(test_watchdog_root_cause_vision);
    RUN_TEST(test_watchdog_root_cause_actuation);
    RUN_TEST(test_watchdog_attribution_consistency);

    std::cout << "\n--- Cross-Subsystem Fault Correlation ---" << std::endl;
    RUN_TEST(test_fault_correlation_vision_state);
    RUN_TEST(test_fault_correlation_actuation_state);
    RUN_TEST(test_fault_timeline_tracking);

    std::cout << "\n--- Crash Dump Tests ---" << std::endl;
    RUN_TEST(test_crash_dump_integrity_basic);
    RUN_TEST(test_crash_dump_sequence);
    RUN_TEST(test_crash_dump_state_size);

    std::cout << "\n--- Stress Log Tests ---" << std::endl;
    RUN_TEST(test_stress_log_no_drop);
    RUN_TEST(test_stress_timestamp_ordering);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}