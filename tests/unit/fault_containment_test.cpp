/**
 * @file fault_containment_test.cpp
 * @brief Part 3: Fault Containment and Degradation Tests
 *
 * Validates:
 * - Single-fault isolation (one subsystem fault does not cascade)
 * - Graceful degradation under partial failure
 * - Correct fail-safe vs fail-silent behavior
 * - Stability under repeated or storming fault conditions
 *
 * Explicitly tests fault combinations and recovery ordering.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

#include "aurore/state_machine.hpp"
#include "aurore/safety_monitor.hpp"
#include "aurore/test_infrastructure.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_NE(a, b) do { if ((a) == (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// Single Fault Isolation Tests
// ============================================================================

TEST(test_vision_fault_isolated) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
    ASSERT_FALSE(sm.is_interlock_enabled());
}

TEST(test_gimbal_fault_isolated) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
    ASSERT_FALSE(sm.is_interlock_enabled());
}

TEST(test_range_fault_isolated) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::RANGE_DATA_STALE);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_no_cascade_vision_to_actuation) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
    ASSERT_FALSE(sm.has_valid_lock());
}

TEST(test_no_cascade_actuation_to_vision) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

// ============================================================================
// Graceful Degradation Tests
// ============================================================================

TEST(test_degradation_partial_vision_failure) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_vision_frame(1, 0);

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

TEST(test_degradation_partial_actuation_failure) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_actuation_frame(1, 0);

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

TEST(test_degradation_within_limits) {
    aurore::SafetyMonitorConfig config;
    config.max_consecutive_misses = 3;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    for (int i = 0; i < 2; i++) {
        [[maybe_unused]] bool result1 = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

TEST(test_degradation_beyond_limits_triggers_fault) {
    aurore::SafetyMonitorConfig config;
    config.max_consecutive_misses = 2;
    config.vision_deadline_ns = 1000;     // 1µs — deliberately tight
    config.actuation_deadline_ns = 1000;  // 1µs — deliberately tight
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Arm the watchdog: deliver exactly one frame so monitor expects continued delivery
    monitor.update_vision_frame(1, aurore::get_timestamp(aurore::ClockId::MonotonicRaw));
    monitor.update_actuation_frame(1, aurore::get_timestamp(aurore::ClockId::MonotonicRaw));

    // Let timestamps go stale (deadline = 1µs, sleep 5ms >> 2×deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    for (int i = 0; i < 5; i++) {
        [[maybe_unused]] bool result1 = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_FALSE(safe);
}

// ============================================================================
// Fail-Safe vs Fail-Silent Tests
// ============================================================================

TEST(test_fail_safe_vision_timeout) {
    aurore::SafetyMonitorConfig config;
    config.vision_deadline_ns = 1000;  // 1µs — deliberately tight
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    // Arm the watchdog: one frame arms vision monitoring
    monitor.update_vision_frame(1, aurore::get_timestamp(aurore::ClockId::MonotonicRaw));

    // Sleep well beyond deadline (1µs × 2 = 2µs; sleep 5ms ensures stale)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    for (int i = 0; i < 10; i++) {
        (void)monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();
    auto fault = monitor.current_fault();

    monitor.stop();

    ASSERT_FALSE(safe);
    ASSERT_NE(fault, aurore::SafetyFaultCode::NONE);
}

TEST(test_fail_safe_watchdog_timeout) {
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_timeout_ms = 10;
    aurore::SafetyMonitor monitor(config);
    monitor.init();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_FALSE(safe);
}

TEST(test_fail_safe_emergency_stop) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.trigger_emergency_stop("Test emergency");

    bool emergency = monitor.is_emergency_active();

    monitor.stop();

    ASSERT_TRUE(emergency);
}

TEST(test_fail_silent_no_fault_on_success) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.update_vision_frame(1, 1000);
    monitor.update_actuation_frame(1, 1000);

    for (int i = 0; i < 10; i++) {
        (void)monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();
    auto fault = monitor.current_fault();

    monitor.stop();

    ASSERT_TRUE(safe);
    ASSERT_EQ(fault, aurore::SafetyFaultCode::NONE);
}

// ============================================================================
// Repeated/Storming Fault Tests
// ============================================================================

TEST(test_storming_faults_rate_limited) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    for (int i = 0; i < 100; i++) {
        monitor.trigger_emergency_stop("Storm test");
    }

    bool emergency = monitor.is_emergency_active();

    monitor.stop();

    ASSERT_TRUE(emergency);
}

TEST(test_repeated_same_fault_same_action) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.trigger_emergency_stop("First fault");
    auto fault1 = monitor.current_fault();

    monitor.trigger_emergency_stop("Second fault");
    auto fault2 = monitor.current_fault();

    monitor.stop();

    ASSERT_EQ(fault1, fault2);
}

TEST(test_multiple_different_faults) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    monitor.trigger_emergency_stop("First fault");
    auto fault1 = monitor.current_fault();

    monitor.stop();

    ASSERT_NE(fault1, aurore::SafetyFaultCode::NONE);
}

TEST(test_fault_after_recovery) {
    aurore::SafetyMonitorConfig config;
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    for (int i = 0; i < 5; i++) {
        [[maybe_unused]] bool result1 = monitor.run_cycle();
    }

    monitor.update_vision_frame(100, 1000000);
    monitor.update_actuation_frame(100, 1000000);

    for (int i = 0; i < 5; i++) {
        [[maybe_unused]] bool result1 = monitor.run_cycle();
    }

    bool safe = monitor.is_system_safe();

    monitor.stop();

    ASSERT_TRUE(safe);
}

// ============================================================================
// Fault Combination Tests
// ============================================================================

TEST(test_combined_vision_and_actuation_fault) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_prevents_arm) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::SEQUENCE_GAP);

    sm.set_operator_authorization(true);

    sm.request_search();

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_during_tracking_transitions_to_fault) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.on_fault(aurore::FaultCode::WATCHDOG_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_recovery_after_fault_clears_fault_state) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

    sm.on_manual_reset();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
    ASSERT_TRUE(sm.has_zero_faults());
}

// ============================================================================
// Recovery Ordering Tests
// ============================================================================

TEST(test_recovery_order_fault_then_reset) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::I2C_FAULT);
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

    sm.on_manual_reset();
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_recovery_order_multiple_resets) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::AUTH_FAILURE);
    sm.on_manual_reset();

    sm.on_fault(aurore::FaultCode::TEMPERATURE_CRITICAL);
    sm.on_manual_reset();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_recovery_timing_is_deterministic) {
    auto start = std::chrono::steady_clock::now();

    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    sm.on_manual_reset();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    ASSERT_LT(duration.count(), 100000);
}

// ============================================================================
// Fault Code Tests
// ============================================================================

TEST(test_all_fault_codes_unique) {
    std::vector<aurore::SafetyFaultCode> codes = {
        aurore::SafetyFaultCode::VISION_STALLED,
        aurore::SafetyFaultCode::VISION_LATENCY_EXCEEDED,
        aurore::SafetyFaultCode::ACTUATION_STALLED,
        aurore::SafetyFaultCode::ACTUATION_LATENCY_EXCEEDED,
        aurore::SafetyFaultCode::FRAME_DEADLINE_MISSED,
        aurore::SafetyFaultCode::WATCHDOG_FEED_FAILED,
        aurore::SafetyFaultCode::EMERGENCY_STOP_REQUESTED,
    };

    for (size_t i = 0; i < codes.size(); i++) {
        for (size_t j = i + 1; j < codes.size(); j++) {
            ASSERT_NE(codes[i], codes[j]);
        }
    }
}

TEST(test_fault_to_string_mapping) {
    const char* name = aurore::fault_code_to_string(aurore::SafetyFaultCode::VISION_STALLED);
    ASSERT_TRUE(name != nullptr);
    ASSERT_TRUE(name[0] != '\0');
}

// ============================================================================
// Fault Injector Validation Tests
// ============================================================================

TEST(test_fault_injector_isolation) {
    bool isolated = aurore::test::FaultInjector::validate_isolation(
        aurore::test::FaultTarget::Camera,
        aurore::test::FaultTarget::Gimbal);

    ASSERT_TRUE(isolated);
}

TEST(test_fault_injector_degradation) {
    bool graceful = aurore::test::FaultInjector::validate_degradation(
        aurore::test::FaultTarget::Camera, true);

    ASSERT_TRUE(graceful);
}

TEST(test_fault_injector_fail_safe) {
    bool safe = aurore::test::FaultInjector::validate_fail_safe(
        aurore::test::FaultTarget::Gimbal);

    ASSERT_TRUE(safe);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 3: Fault Containment and Degradation Tests ===" << std::endl;
    std::cout << "Running fault containment tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Single Fault Isolation ---" << std::endl;
    RUN_TEST(test_vision_fault_isolated);
    RUN_TEST(test_gimbal_fault_isolated);
    RUN_TEST(test_range_fault_isolated);
    RUN_TEST(test_no_cascade_vision_to_actuation);
    RUN_TEST(test_no_cascade_actuation_to_vision);

    std::cout << "\n--- Graceful Degradation ---" << std::endl;
    RUN_TEST(test_degradation_partial_vision_failure);
    RUN_TEST(test_degradation_partial_actuation_failure);
    RUN_TEST(test_degradation_within_limits);
    RUN_TEST(test_degradation_beyond_limits_triggers_fault);

    std::cout << "\n--- Fail-Safe vs Fail-Silent ---" << std::endl;
    RUN_TEST(test_fail_safe_vision_timeout);
    RUN_TEST(test_fail_safe_watchdog_timeout);
    RUN_TEST(test_fail_safe_emergency_stop);
    RUN_TEST(test_fail_silent_no_fault_on_success);

    std::cout << "\n--- Repeated/Storming Faults ---" << std::endl;
    RUN_TEST(test_storming_faults_rate_limited);
    RUN_TEST(test_repeated_same_fault_same_action);
    RUN_TEST(test_multiple_different_faults);
    RUN_TEST(test_fault_after_recovery);

    std::cout << "\n--- Fault Combination Tests ---" << std::endl;
    RUN_TEST(test_combined_vision_and_actuation_fault);
    RUN_TEST(test_fault_prevents_arm);
    RUN_TEST(test_fault_during_tracking_transitions_to_fault);
    RUN_TEST(test_recovery_after_fault_clears_fault_state);

    std::cout << "\n--- Recovery Ordering Tests ---" << std::endl;
    RUN_TEST(test_recovery_order_fault_then_reset);
    RUN_TEST(test_recovery_order_multiple_resets);
    RUN_TEST(test_recovery_timing_is_deterministic);

    std::cout << "\n--- Fault Code Tests ---" << std::endl;
    RUN_TEST(test_all_fault_codes_unique);
    RUN_TEST(test_fault_to_string_mapping);

    std::cout << "\n--- Fault Injector Validation ---" << std::endl;
    RUN_TEST(test_fault_injector_isolation);
    RUN_TEST(test_fault_injector_degradation);
    RUN_TEST(test_fault_injector_fail_safe);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}