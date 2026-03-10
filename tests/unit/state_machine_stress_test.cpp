#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/state_machine.hpp"
#include "aurore/timing.hpp"

namespace {

// Test counters
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
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

}  // anonymous namespace

using namespace aurore;

// 31. 3-Frame Stability
TEST(test_3_frame_stability) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    
    Detection d;
    d.confidence = 0.99f;
    d.bbox = {100, 100, 10, 10}; 
    
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
}

// 32. Stability Reset
TEST(test_stability_reset) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    
    Detection d1 = {0.99f, {100, 100, 10, 10}};
    Detection d2 = {0.99f, {100, 100, 10, 10}};
    Detection d3_unstable = {0.99f, {200, 200, 10, 10}};
    
    sm.on_detection(d1);
    sm.on_detection(d2);
    sm.on_detection(d3_unstable); // stable_frame_count becomes 0
    
    sm.on_detection(d1); // stable_frame_count becomes 0 (dist from d3 is large)
    sm.on_detection(d1); // stable_frame_count becomes 1 (dist is 0)
    sm.on_detection(d1); // stable_frame_count becomes 2
    ASSERT_EQ(sm.state(), FcsState::SEARCH);
    
    sm.on_detection(d1); // stable_frame_count becomes 3
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
}

// 33. Lock Confirmation Window
TEST(test_lock_confirmation_window) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    sm.set_operator_authorization(true);
    
    Detection d = {0.99f, {100, 100, 10, 10}};
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
    
    // Need redetection score for has_valid_lock()
    sm.on_redetection_score(0.99f);
    
    // Tick 32 frames (32 * 8ms = 256ms)
    for (int i = 0; i < 32; ++i) {
        sm.tick(std::chrono::milliseconds(8));
        sm.on_detection(d);
    }
    
    sm.on_gimbal_status({0.1f, 0.1f, 0.1f, 10});
    RangeData rd = {100.0f, get_timestamp(), 0};
    rd.checksum = StateMachine::compute_crc16(rd.range_m, rd.timestamp_ns);
    sm.on_lrf_range(rd);
    
    sm.on_ballistics_solution({0, 0, 100.0f, 0, 0.99f, true});
    
    ASSERT_EQ(sm.state(), FcsState::ARMED);
}

// 34. Operator Auth Revocation
TEST(test_operator_auth_revocation) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    sm.set_operator_authorization(true);
    
    Detection d = {0.99f, {100, 100, 10, 10}};
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_redetection_score(0.99f);
    
    for (int i = 0; i < 32; ++i) {
        sm.tick(std::chrono::milliseconds(8));
        sm.on_detection(d);
    }
    
    sm.on_gimbal_status({0.1f, 0.1f, 0.1f, 10});
    RangeData rd = {100.0f, get_timestamp(), 0};
    rd.checksum = StateMachine::compute_crc16(rd.range_m, rd.timestamp_ns);
    sm.on_lrf_range(rd);
    sm.on_ballistics_solution({0, 0, 100.0f, 0, 0.99f, true});
    
    ASSERT_EQ(sm.state(), FcsState::ARMED);
    
    sm.set_operator_authorization(false);
    ASSERT_EQ(sm.state(), FcsState::IDLE_SAFE);
}

// 35. Manual Reset Trap
TEST(test_manual_reset_trap) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    ASSERT_EQ(sm.state(), FcsState::SEARCH);
    sm.on_manual_reset();
    ASSERT_EQ(sm.state(), FcsState::SEARCH);
}

// 36. Invalid CRC
TEST(test_invalid_crc) {
    StateMachine sm;
    RangeData rd = {100.0f, get_timestamp(), 0};
    rd.checksum = StateMachine::compute_crc16(rd.range_m, rd.timestamp_ns) + 1;
    sm.on_lrf_range(rd);
    ASSERT_EQ(sm.state(), FcsState::FAULT);
}

// 37. Range Bounds
TEST(test_range_bounds) {
    StateMachine sm;
    sm.on_init_complete();
    RangeData rd = {0.49f, get_timestamp(), 0};
    rd.checksum = StateMachine::compute_crc16(rd.range_m, rd.timestamp_ns);
    sm.on_lrf_range(rd);
    ASSERT_EQ(sm.state(), FcsState::FAULT);
}

// 38. State Age Overflow
TEST(test_state_age_overflow) {
    StateMachine sm;
    sm.tick(std::chrono::hours(25));
    ASSERT_EQ(sm.state(), FcsState::BOOT);
}

// 39. Boot Failure
TEST(test_boot_failure) {
    StateMachine sm;
    sm.on_boot_failure();
    ASSERT_EQ(sm.state(), FcsState::FAULT);
}

// 40. Search Timeout
TEST(test_search_timeout) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    sm.tick(std::chrono::milliseconds(5001));
    ASSERT_EQ(sm.state(), FcsState::IDLE_SAFE);
}

// 41. Gimbal Settle Boundary
TEST(test_gimbal_settle_boundary) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    Detection d = {0.99f, {100, 100, 10, 10}};
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
}

// 43. Position History Wrap
TEST(test_position_history_wrap) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    Detection d = {0.99f, {100, 100, 10, 10}};
    for (int i = 0; i < 10; ++i) sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
}

// 44. Detection Confidence
TEST(test_detection_confidence) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    Detection d = {0.949f, {100, 100, 10, 10}};
    sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::SEARCH);
}

// 45. Interlock Persistence (Verify it stays DISABLED as per spec)
TEST(test_interlock_persistence) {
    StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    ASSERT_FALSE(sm.is_interlock_enabled());
    
    Detection d = {0.99f, {100, 100, 10, 10}};
    sm.on_detection(d);
    sm.on_detection(d);
    sm.on_detection(d);
    ASSERT_EQ(sm.state(), FcsState::TRACKING);
    ASSERT_FALSE(sm.is_interlock_enabled()); // Should stay disabled
}

int main() {
    std::cout << "Running State Machine Stress tests..." << std::endl;
    RUN_TEST(test_3_frame_stability);
    RUN_TEST(test_stability_reset);
    RUN_TEST(test_lock_confirmation_window);
    RUN_TEST(test_operator_auth_revocation);
    RUN_TEST(test_manual_reset_trap);
    RUN_TEST(test_invalid_crc);
    RUN_TEST(test_range_bounds);
    RUN_TEST(test_state_age_overflow);
    RUN_TEST(test_boot_failure);
    RUN_TEST(test_search_timeout);
    RUN_TEST(test_gimbal_settle_boundary);
    RUN_TEST(test_position_history_wrap);
    RUN_TEST(test_detection_confidence);
    RUN_TEST(test_interlock_persistence);
    
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
