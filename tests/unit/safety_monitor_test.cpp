/**
 * @file safety_monitor_test.cpp
 * @brief Unit tests for SafetyMonitor
 *
 * Tests cover:
 * - Fault detection and reporting
 * - Vision/actuation health monitoring
 * - Emergency stop triggering
 * - Callback invocation
 */

#include "aurore/safety_monitor.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Test counters
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

// Helper: sleep for specified duration in milliseconds
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // anonymous namespace

// ============================================================================
// Basic Tests
// ============================================================================

TEST(test_safety_monitor_construction) {
    aurore::SafetyMonitorConfig config;
    config.vision_deadline_ns = 20000000;
    config.actuation_deadline_ns = 2000000;

    aurore::SafetyMonitor monitor(config);

    // Fresh monitor should be safe with no faults
    ASSERT_TRUE(monitor.is_system_safe());
    ASSERT_FALSE(monitor.is_emergency_active());
    // current_fault() returns NONE by default via atomic initialization
}

TEST(test_safety_monitor_init) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;  // Disable for test

    aurore::SafetyMonitor monitor(config);

    // Init should succeed even without hardware watchdog
    bool result = monitor.init();
    ASSERT_TRUE(result);
}

TEST(test_safety_monitor_start_stop) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    ASSERT_FALSE(monitor.is_running());

    monitor.start();
    ASSERT_TRUE(monitor.is_running());

    monitor.stop();
    ASSERT_FALSE(monitor.is_running());
}

// ============================================================================
// Fault Detection Tests
// ============================================================================

TEST(test_safety_monitor_vision_update) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 100000000;  // 100ms for test

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Update vision frames
    auto ts = aurore::get_timestamp();
    monitor.update_vision_frame(1, ts);

    ASSERT_TRUE(monitor.is_system_safe());

    // Run monitoring cycle
    ASSERT_TRUE(monitor.run_cycle());
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_safety_monitor_actuation_update) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.actuation_deadline_ns = 100000000;  // 100ms for test

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Update actuation frames
    auto ts = aurore::get_timestamp();
    monitor.update_actuation_frame(1, ts);

    ASSERT_TRUE(monitor.is_system_safe());

    ASSERT_TRUE(monitor.run_cycle());
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_safety_monitor_vision_latency_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 1000000;  // 1ms - very short for test

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Update with old timestamp
    auto old_ts = aurore::get_timestamp() - 10000000;  // 10ms ago
    monitor.update_vision_frame(1, old_ts);

    // Run cycle - should detect latency fault
    sleep_ms(10);
    bool run_cycle_result = monitor.run_cycle();
    std::cout << "  run_cycle_result: " << std::boolalpha << run_cycle_result << std::endl;
    std::fflush(stdout);
    std::cout << "  is_system_safe: " << std::boolalpha << monitor.is_system_safe() << std::endl;
    std::fflush(stdout);
    std::cout << "  current_fault: " << static_cast<int>(monitor.current_fault()) << " ("
              << aurore::fault_code_to_string(monitor.current_fault()) << ")" << std::endl;
    std::fflush(stdout);
    ASSERT_FALSE(run_cycle_result);

    // May or may not trigger fault depending on timing
    // Just verify monitor is still running
    ASSERT_FALSE(monitor.is_system_safe());  // This should be false if a fault is detected
    ASSERT_EQ(monitor.current_fault(), aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED);

    monitor.stop();
}

TEST(test_safety_monitor_emergency_stop) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Trigger emergency stop
    monitor.trigger_emergency_stop("Test emergency");

    ASSERT_FALSE(monitor.is_system_safe());
    ASSERT_TRUE(monitor.is_emergency_active());
    ASSERT_EQ(monitor.current_fault(), aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED);

    monitor.stop();
}

TEST(test_safety_monitor_fault_clear) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Trigger fault (non-emergency)
    // Note: trigger_emergency_stop sets emergency_active, which can't be cleared

    // Verify clear_fault returns false (faults are latched, non-clearable per AM7-L3-SAFE-006)
    bool cleared = monitor.clear_fault();
    ASSERT_FALSE(cleared);
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

// ============================================================================
// Callback Tests
// ============================================================================

TEST(test_safety_monitor_safety_callback) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    std::atomic<bool> callback_invoked(false);
    std::atomic<aurore::SafetyFaultCode> callback_code(aurore::SafetyFaultCode::NONE);

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Use a static function wrapper for the callback
    [[maybe_unused]] auto* p_invoked = &callback_invoked;
    [[maybe_unused]] auto* p_code = &callback_code;

    monitor.set_safety_action_callback(
        [](aurore::SafetyFaultCode /*code*/, const char* /*reason*/, void* user_data) {
            auto* invoked = static_cast<std::atomic<bool>*>(user_data);
            invoked->store(true);
        },
        &callback_invoked);

    // Trigger emergency - should invoke callback
    monitor.trigger_emergency_stop("Test");

    // Give callback time to execute
    sleep_ms(10);

    ASSERT_TRUE(callback_invoked.load());

    monitor.stop();
}

TEST(test_safety_monitor_log_callback) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    std::atomic<size_t> log_count(0);

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.set_log_callback(
        [](const aurore::SafetyEvent& /*event*/, void* user_data) {
            auto* count = static_cast<std::atomic<size_t>*>(user_data);
            count->fetch_add(1);
        },
        &log_count);

    // Trigger fault - should log
    monitor.trigger_emergency_stop("Test");

    sleep_ms(10);

    ASSERT_TRUE(log_count.load() >= 1);

    monitor.stop();
}

// ============================================================================
// Fault Code Tests
// ============================================================================

TEST(test_fault_code_to_string) {
    // Test all known fault codes
    ASSERT_TRUE(aurore::fault_code_to_string(aurore::SafetyFaultCode::NONE) != nullptr);
    ASSERT_TRUE(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_STALLED) != nullptr);
    ASSERT_TRUE(aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED) !=
                nullptr);
    ASSERT_TRUE(aurore::fault_code_to_string(aurore::SafetyFaultCode::ACTUATION_STALLED) !=
                nullptr);
    ASSERT_TRUE(aurore::fault_code_to_string(aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED) !=
                nullptr);

    // Verify specific strings
    ASSERT_EQ(std::string(aurore::fault_code_to_string(aurore::SafetyFaultCode::NONE)), "NONE");
    ASSERT_EQ(std::string(
                  aurore::fault_code_to_string(aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED)),
              "EMERGENCY_STOP_REQUESTED");
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(test_safety_monitor_rapid_updates) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 1000000000;  // 1s - very relaxed for test
    config.actuation_deadline_ns = 1000000000;
    config.max_consecutive_misses = 100;  // Allow many misses

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    std::atomic<bool> stop(false);

    // Rapid vision updates
    std::thread updater([&]() {
        uint64_t seq = 0;
        while (!stop.load()) {
            monitor.update_vision_frame(seq++, aurore::get_timestamp());
            std::this_thread::yield();
        }
    });

    // Run monitoring cycles
    for (int i = 0; i < 100; i++) {
        ASSERT_TRUE(monitor.run_cycle());
        std::this_thread::sleep_for(std::chrono::microseconds(100));  // Small delay
    }

    stop.store(true);
    updater.join();

    // System should be safe (may have had some misses but recovered)
    // Just verify monitor is still running and functional
    ASSERT_TRUE(monitor.is_running());

    monitor.stop();
}

TEST(test_safety_monitor_concurrent_access) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;
    config.vision_deadline_ns = 100000000;
    config.actuation_deadline_ns = 100000000;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    constexpr int kNumThreads = 4;
    std::vector<std::thread> threads;

    // Multiple threads updating vision
    for (int t = 0; t < kNumThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; i++) {
                monitor.update_vision_frame(static_cast<uint64_t>(t * 100 + i),
                                            aurore::get_timestamp());
                std::this_thread::yield();
            }
        });
    }

    // Multiple threads updating actuation
    for (int t = 0; t < kNumThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; i++) {
                monitor.update_actuation_frame(static_cast<uint64_t>(t * 100 + i),
                                               aurore::get_timestamp());
                std::this_thread::yield();
            }
        });
    }

    // Monitoring thread
    threads.emplace_back([&]() {
        for (int i = 0; i < 500; i++) {
            ASSERT_TRUE(monitor.run_cycle());
            std::this_thread::yield();
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    // Monitor should still be running
    ASSERT_TRUE(monitor.is_running());

    monitor.stop();
}

// ============================================================================
// SafetyEvent Tests
// ============================================================================

TEST(test_safety_event_structure) {
    aurore::SafetyEvent event;

    ASSERT_EQ(event.timestamp_ns, 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::NONE);
    ASSERT_EQ(event.severity, 0);

    // Test setting values
    event.timestamp_ns = aurore::get_timestamp();
    event.fault_code = aurore::SafetyFaultCode::VISION_STALLED;
    event.severity = 3;

    ASSERT_TRUE(event.timestamp_ns > 0);
    ASSERT_EQ(event.fault_code, aurore::SafetyFaultCode::VISION_STALLED);
    ASSERT_EQ(event.severity, 3);
}

// ============================================================================
// Software Watchdog Tests
// ============================================================================

TEST(test_software_watchdog_construction) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 60;

    aurore::SafetyMonitor monitor(config);

    // Fresh monitor should be safe
    ASSERT_TRUE(monitor.is_system_safe());
}

TEST(test_software_watchdog_init_starts_thread) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 60;

    aurore::SafetyMonitor monitor(config);
    monitor.init();

    // Give thread time to start
    sleep_ms(10);

    // Monitor should still be safe (initial kick set by init)
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_software_watchdog_kick_prevents_timeout) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 100;  // 100ms timeout for test

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Regularly kick the watchdog
    for (int i = 0; i < 10; i++) {
        monitor.kick_watchdog();
        sleep_ms(20);  // Kick every 20ms (well under 100ms timeout)
    }

    // System should still be safe
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_software_watchdog_timeout_triggers_fault) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 50;  // 50ms timeout

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Initial kick
    monitor.kick_watchdog();

    // Wait for timeout (no more kicks)
    sleep_ms(100);

    // Run a monitoring cycle to allow watchdog thread to detect timeout
    bool run_cycle_result = monitor.run_cycle();
    std::cout << "  run_cycle_result: " << std::boolalpha << run_cycle_result << std::endl;
    std::fflush(stdout);
    std::cout << "  is_system_safe: " << std::boolalpha << monitor.is_system_safe() << std::endl;
    std::fflush(stdout);
    std::cout << "  current_fault: " << static_cast<int>(monitor.current_fault()) << " ("
              << aurore::fault_code_to_string(monitor.current_fault()) << ")" << std::endl;
    std::fflush(stdout);
    ASSERT_FALSE(run_cycle_result);

    // Should have triggered watchdog fault
    // Note: May need to wait a bit longer for watchdog thread to detect
    sleep_ms(50);

    // Check if fault was triggered (watchdog thread runs at 10ms interval)
    // The fault should be WATCHDOG_FEED_FAILED
    [[maybe_unused]] auto fault = monitor.current_fault();

    // Either watchdog triggered or system is still safe (timing dependent)
    // Just verify monitor is still functional
    ASSERT_FALSE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_software_watchdog_disabled) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // No kicks - should remain safe since watchdog is disabled
    sleep_ms(100);

    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_watchdog_kick_raii) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 100;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Use RAII kick in a scope
    {
        aurore::WatchdogKick kick(monitor);
        // Kick happens at end of scope
    }

    // System should be safe
    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_watchdog_kick_raii_multiple) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = 50;
    config.watchdog_timeout_ms = 100;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Multiple RAII kicks
    for (int i = 0; i < 5; i++) {
        {
            aurore::WatchdogKick kick(monitor);
        }
        sleep_ms(10);
    }

    ASSERT_TRUE(monitor.is_system_safe());

    monitor.stop();
}

TEST(test_software_watchdog_config_defaults) {
    aurore::SafetyMonitorConfig config;

    // Check defaults
    ASSERT_EQ(config.watchdog_kick_interval_ms, 50);
    ASSERT_EQ(config.watchdog_timeout_ms, 60);
    ASSERT_EQ(config.enable_watchdog, true);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Running SafetyMonitor tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    // Basic tests
    RUN_TEST(test_safety_monitor_construction);
    RUN_TEST(test_safety_monitor_init);
    RUN_TEST(test_safety_monitor_start_stop);

    // Fault detection tests
    RUN_TEST(test_safety_monitor_vision_update);
    RUN_TEST(test_safety_monitor_actuation_update);
    RUN_TEST(test_safety_monitor_vision_latency_fault);
    RUN_TEST(test_safety_monitor_emergency_stop);
    RUN_TEST(test_safety_monitor_fault_clear);

    // Callback tests
    RUN_TEST(test_safety_monitor_safety_callback);
    RUN_TEST(test_safety_monitor_log_callback);

    // Fault code tests
    RUN_TEST(test_fault_code_to_string);

    // Stress tests
    RUN_TEST(test_safety_monitor_rapid_updates);
    RUN_TEST(test_safety_monitor_concurrent_access);

    // Structure tests
    RUN_TEST(test_safety_event_structure);

    // Software watchdog tests
    RUN_TEST(test_software_watchdog_construction);
    RUN_TEST(test_software_watchdog_init_starts_thread);
    RUN_TEST(test_software_watchdog_kick_prevents_timeout);
    RUN_TEST(test_software_watchdog_timeout_triggers_fault);
    RUN_TEST(test_software_watchdog_disabled);
    RUN_TEST(test_watchdog_kick_raii);
    RUN_TEST(test_watchdog_kick_raii_multiple);
    RUN_TEST(test_software_watchdog_config_defaults);

    // Summary
    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}
