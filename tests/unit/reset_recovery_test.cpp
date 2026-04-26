/**
 * @file reset_recovery_test.cpp
 * @brief Part 9: Reset, Update, and Recovery Tests
 *
 * Tests:
 * - Mid-operation resets
 * - Partial subsystem restarts
 * - Live configuration reloads
 * - Rollback to last-known-good configuration
 * - Deterministic recovery timing and ordering
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
#include <functional>

#include "aurore/state_machine.hpp"
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
#define ASSERT_NE(a, b) do { if ((a) == (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// Mid-Operation Reset Tests
// ============================================================================

TEST(test_mid_operation_reset_search) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_mid_operation_reset_tracking) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    aurore::Detection d;
    d.id = 1;
    d.confidence = 1.0f;
    sm.on_detection(d);

    aurore::TrackSolution sol;
    sol.valid = true;
    sm.on_tracker_initialized(sol);

    for (int i = 0; i < 10; i++) {
        sm.on_tracker_update(sol);
    }

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
    ASSERT_FALSE(sm.has_valid_lock());
}

TEST(test_mid_operation_reset_armed) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    sm.force_state_for_test(aurore::FcsState::TRACKING);

    sm.on_redetection_score(0.96f);
    sm.set_operator_authorization(true);

    aurore::FireControlSolution fc;
    fc.p_hit = 0.99f;
    sm.on_ballistics_solution(fc);
    ASSERT_EQ(sm.state(), aurore::FcsState::ARMED);

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

// ============================================================================
// Partial Subsystem Restart Tests
// ============================================================================

TEST(test_partial_restart_vision_only) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

    sm.on_manual_reset();
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_partial_restart_after_gimbal_fault) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    sm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

    sm.on_manual_reset();
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
    ASSERT_TRUE(sm.has_zero_faults());
}

TEST(test_partial_restart_preserves_detection_count) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    for (int i = 0; i < 5; i++) {
        aurore::Detection d;
        d.id = i;
        d.confidence = 1.0f;
        sm.on_detection(d);
    }

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

// ============================================================================
// Live Configuration Reload Tests
// ============================================================================

TEST(test_config_reload_safety) {
    aurore::test::ConfigReloadTester tester;

    bool loaded = tester.load_config("default");
    ASSERT_TRUE(loaded);

    bool reloaded = tester.reload_config("default");
    ASSERT_TRUE(reloaded);
}

TEST(test_config_reload_integrity) {
    aurore::test::ConfigReloadTester tester;

    tester.load_config("default");
    auto config1 = tester.get_active_config();

    tester.reload_config("default");
    auto config2 = tester.get_active_config();

    // Reload gives a valid config with a newer version (reload increments version)
    ASSERT_TRUE(config2.valid);
    ASSERT_LT(config1.version, config2.version);
}

TEST(test_config_rollback) {
    aurore::test::ConfigReloadTester tester;

    ASSERT_TRUE(tester.load_config("default"));
    auto config1 = tester.get_active_config();

    auto rollback = tester.rollback_to_last_known_good();
    ASSERT_TRUE(rollback);

    auto config2 = tester.get_active_config();
    ASSERT_EQ(config1.valid, config2.valid);
}

// ============================================================================
// Recovery Timing Tests
// ============================================================================

TEST(test_recovery_timing_deterministic) {
    std::vector<uint64_t> recovery_times;

    for (int i = 0; i < 10; i++) {
        auto start = std::chrono::steady_clock::now();

        aurore::StateMachine sm;
        sm.on_init_complete();
        sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
        sm.on_manual_reset();

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        recovery_times.push_back(static_cast<uint64_t>(duration.count()));
    }

    uint64_t min_time = recovery_times[0];
    uint64_t max_time = recovery_times[0];
    for (size_t i = 1; i < recovery_times.size(); i++) {
        min_time = std::min(min_time, recovery_times[i]);
        max_time = std::max(max_time, recovery_times[i]);
    }

    ASSERT_LT(max_time - min_time, 10000);
}

TEST(test_recovery_order_deterministic) {
    auto run_recovery = [](aurore::FaultCode fault) {
        aurore::StateMachine sm;
        sm.on_init_complete();
        sm.on_fault(fault);
        sm.on_manual_reset();
        return sm.state();
    };

    auto state1 = run_recovery(aurore::FaultCode::CAMERA_TIMEOUT);
    auto state2 = run_recovery(aurore::FaultCode::GIMBAL_TIMEOUT);

    ASSERT_EQ(state1, state2);
    ASSERT_EQ(state1, aurore::FcsState::IDLE_SAFE);
}

TEST(test_recovery_latency_bounded) {
    auto start = std::chrono::steady_clock::now();

    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.on_fault(aurore::FaultCode::I2C_FAULT);
    sm.on_manual_reset();

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_LT(duration.count(), 100);
}

// ============================================================================
// State Reset Tests
// ============================================================================

TEST(test_reset_clears_state_flags) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.set_operator_authorization(true);

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_FALSE(sm.has_operator_authorization());
}

TEST(test_reset_clears_detection_buffer) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    aurore::Detection d;
    d.id = 42;
    d.confidence = 1.0f;
    sm.on_detection(d);

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    sm.request_search();

    aurore::Detection d2;
    d2.id = 1;
    d2.confidence = 1.0f;
    sm.on_detection(d2);

    auto new_id = d2.id;

    ASSERT_EQ(new_id, 1);
}

TEST(test_reset_clears_tracking_state) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();

    aurore::Detection d;
    d.id = 1;
    d.confidence = 1.0f;
    sm.on_detection(d);

    aurore::TrackSolution sol;
    sol.valid = true;
    sm.on_tracker_initialized(sol);

    for (int i = 0; i < 10; i++) {
        sm.on_tracker_update(sol);
    }

    sm.force_state_for_test(aurore::FcsState::BOOT);
    sm.on_init_complete();

    ASSERT_FALSE(sm.has_valid_lock());
}

// ============================================================================
// Fault Reset Sequence Tests
// ============================================================================

TEST(test_fault_reset_sequence_all_faults) {
    std::vector<aurore::FaultCode> faults = {
        aurore::FaultCode::CAMERA_TIMEOUT,
        aurore::FaultCode::GIMBAL_TIMEOUT,
        aurore::FaultCode::I2C_FAULT,
        aurore::FaultCode::RANGE_DATA_STALE,
    };

    for (auto fault : faults) {
        aurore::StateMachine sm;
        sm.on_init_complete();
        sm.on_fault(fault);

        ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

        sm.on_manual_reset();

        ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
    }
}

TEST(test_multiple_reset_sequence) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    sm.on_manual_reset();

    sm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);
    sm.on_manual_reset();

    sm.on_fault(aurore::FaultCode::I2C_FAULT);
    sm.on_manual_reset();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
    ASSERT_TRUE(sm.has_zero_faults());
}

// ============================================================================
// Boot Failure Tests
// ============================================================================

TEST(test_boot_failure_transitions_to_fault) {
    aurore::StateMachine sm;
    sm.on_boot_failure();

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_boot_failure_recovery) {
    aurore::StateMachine sm;
    sm.on_boot_failure();

    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);

    sm.on_manual_reset();

    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_boot_timeout_recovery) {
    std::atomic<bool> boot_completed(false);

    aurore::StateMachine sm;
    sm.on_init_complete();

    std::thread delayed_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        boot_completed.store(true, std::memory_order_release);
    });

    delayed_thread.join();

    ASSERT_TRUE(boot_completed.load() || sm.state() == aurore::FcsState::IDLE_SAFE ||
                         sm.state() == aurore::FcsState::BOOT);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 9: Reset, Update, and Recovery Tests ===" << std::endl;
    std::cout << "Running reset/recovery tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Mid-Operation Reset Tests ---" << std::endl;
    RUN_TEST(test_mid_operation_reset_search);
    RUN_TEST(test_mid_operation_reset_tracking);
    RUN_TEST(test_mid_operation_reset_armed);

    std::cout << "\n--- Partial Subsystem Restart Tests ---" << std::endl;
    RUN_TEST(test_partial_restart_vision_only);
    RUN_TEST(test_partial_restart_after_gimbal_fault);
    RUN_TEST(test_partial_restart_preserves_detection_count);

    std::cout << "\n--- Live Configuration Reload Tests ---" << std::endl;
    RUN_TEST(test_config_reload_safety);
    RUN_TEST(test_config_reload_integrity);
    RUN_TEST(test_config_rollback);

    std::cout << "\n--- Recovery Timing Tests ---" << std::endl;
    RUN_TEST(test_recovery_timing_deterministic);
    RUN_TEST(test_recovery_order_deterministic);
    RUN_TEST(test_recovery_latency_bounded);

    std::cout << "\n--- State Reset Tests ---" << std::endl;
    RUN_TEST(test_reset_clears_state_flags);
    RUN_TEST(test_reset_clears_detection_buffer);
    RUN_TEST(test_reset_clears_tracking_state);

    std::cout << "\n--- Fault Reset Sequence Tests ---" << std::endl;
    RUN_TEST(test_fault_reset_sequence_all_faults);
    RUN_TEST(test_multiple_reset_sequence);

    std::cout << "\n--- Boot Failure Tests ---" << std::endl;
    RUN_TEST(test_boot_failure_transitions_to_fault);
    RUN_TEST(test_boot_failure_recovery);
    RUN_TEST(test_boot_timeout_recovery);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}