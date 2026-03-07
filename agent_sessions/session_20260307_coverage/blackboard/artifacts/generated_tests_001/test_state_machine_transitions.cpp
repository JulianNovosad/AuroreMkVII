/**
 * @file test_state_machine_transitions.cpp
 * @brief Unit tests for all state machine transition edge cases
 *
 * Tests cover:
 * - Invalid ARMED transition rejection (AM7-L3-MODE-009)
 * - FAULT state latching
 * - SEARCH/ARMED timeout transitions
 * - Tracker update invalid solution path
 * - Gimbal status unsettled path
 * - Fire command from non-ARMED state
 *
 * Coverage gaps addressed:
 * - src/state_machine/state_machine.cpp lines 45-195
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "aurore/state_machine.hpp"

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

// ============================================================================
// AM7-L3-MODE-009: Invalid ARMED Transition Tests
// ============================================================================

TEST(test_armed_only_from_tracking) {
    // AM7-L3-MODE-009: No state shall transition directly to ARMED except TRACKING
    
    aurore::StateMachine sm;
    
    // Try to transition from IDLE_SAFE to ARMED (should be rejected)
    sm.force_state_for_test(aurore::FcsState::IDLE_SAFE);
    sm.set_operator_authorization(true);
    
    aurore::FireControlSolution sol;
    sol.p_hit = 0.96f;  // Above threshold
    sm.on_ballistics_solution(sol);
    
    // Should NOT transition to ARMED from IDLE_SAFE
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_armed_rejected_from_freecam) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::FREECAM);
    sm.set_operator_authorization(true);
    
    aurore::FireControlSolution sol;
    sol.p_hit = 0.96f;
    sm.on_ballistics_solution(sol);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FREECAM);
}

TEST(test_armed_rejected_from_search) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    sm.set_operator_authorization(true);
    
    aurore::FireControlSolution sol;
    sol.p_hit = 0.96f;
    sm.on_ballistics_solution(sol);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_armed_allowed_from_tracking) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    sm.set_operator_authorization(true);
    
    // Set up valid lock conditions
    sm.on_redetection_score(0.90f);  // Above kRedetectionScoreMin (0.85)
    
    aurore::FireControlSolution sol;
    sol.p_hit = 0.96f;  // Above kPHitMin (0.95)
    sm.on_ballistics_solution(sol);
    
    // Should transition to ARMED
    ASSERT_EQ(sm.state(), aurore::FcsState::ARMED);
}

// ============================================================================
// FAULT State Latching Tests (AM7-L2-MODE-007)
// ============================================================================

TEST(test_fault_latching_from_idle_safe) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::IDLE_SAFE);
    
    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_latching_from_tracking) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    sm.on_fault(aurore::FaultCode::WATCHDOG_TIMEOUT);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_latching_from_armed) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    
    sm.on_fault(aurore::FaultCode::TEMPERATURE_CRITICAL);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_latching_from_search) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    sm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_latching_from_freecam) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::FREECAM);
    
    sm.on_fault(aurore::FaultCode::RANGE_DATA_STALE);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

TEST(test_fault_latching_interlock_inhibit) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    sm.set_interlock_enabled(true);
    
    ASSERT_TRUE(sm.is_interlock_enabled());
    
    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    
    // FAULT state should force interlock inhibit
    ASSERT_FALSE(sm.is_interlock_enabled());
}

TEST(test_fault_latched_persistent) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    sm.on_fault(aurore::FaultCode::SEQUENCE_GAP);
    
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
    
    // Try to transition out (should fail - latched)
    sm.request_freecam();
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
    
    sm.request_search();
    ASSERT_EQ(sm.state(), aurore::FcsState::FAULT);
}

// ============================================================================
// SEARCH Timeout Tests (AM7-L2-MODE-004)
// ============================================================================

TEST(test_search_timeout_transition) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    // Tick past timeout (kSearchTimeoutMs = 5000)
    sm.tick(std::chrono::milliseconds(5100));
    
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_search_no_timeout_before_limit) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    // Tick just before timeout
    sm.tick(std::chrono::milliseconds(4900));
    
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_search_detection_before_timeout) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    // Receive detection before timeout
    aurore::Detection d;
    d.confidence = 0.8f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    
    // Should transition to TRACKING
    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

// ============================================================================
// ARMED Timeout Tests (AM7-L2-MODE-006)
// ============================================================================

TEST(test_armed_timeout_transition) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    
    // Tick past timeout (kArmedTimeoutMs = 100)
    sm.tick(std::chrono::milliseconds(110));
    
    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

TEST(test_armed_no_timeout_before_limit) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    
    // Tick just before timeout
    sm.tick(std::chrono::milliseconds(90));
    
    ASSERT_EQ(sm.state(), aurore::FcsState::ARMED);
}

// ============================================================================
// Tracker Update Invalid Solution Tests
// ============================================================================

TEST(test_tracker_update_invalid_solution) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    aurore::TrackSolution sol;
    sol.valid = false;
    sm.on_tracker_update(sol);
    
    // Should transition back to SEARCH on lost lock
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_tracker_update_low_psr) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    aurore::TrackSolution sol;
    sol.valid = true;
    sol.psr = 0.02f;  // Below kPsrFailThreshold (0.035)
    sm.on_tracker_update(sol);
    
    // Should transition back to SEARCH
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_tracker_update_valid_solution) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    aurore::TrackSolution sol;
    sol.valid = true;
    sol.psr = 0.05f;  // Above threshold
    sm.on_tracker_update(sol);
    
    // Should stay in TRACKING
    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

// ============================================================================
// Gimbal Status Tests
// ============================================================================

TEST(test_gimbal_status_settled) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    // Send settled gimbal status
    for (int i = 0; i < 5; i++) {
        aurore::GimbalStatus g;
        g.az_error_deg = 1.0f;  // Below kGimbalErrorMaxDeg (2.0)
        g.velocity_deg_s = 2.0f;  // Below kGimbalVelocityMaxDs (5.0)
        g.settled_frames = i;
        sm.on_gimbal_status(g);
    }
    
    // Should still be in SEARCH (waiting for detection)
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_gimbal_status_unsettled) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::SEARCH);
    
    // Send unsettled gimbal status
    aurore::GimbalStatus g;
    g.az_error_deg = 5.0f;  // Above threshold
    g.velocity_deg_s = 10.0f;  // Above threshold
    g.settled_frames = 0;
    sm.on_gimbal_status(g);
    
    // Settled frames should reset
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_gimbal_status_armed_alignment) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    
    // Send good alignment status
    for (int i = 0; i < 5; i++) {
        aurore::GimbalStatus g;
        g.az_error_deg = 0.3f;  // Below kAlignErrorMaxDeg (0.5)
        sm.on_gimbal_status(g);
        sm.tick(std::chrono::milliseconds(8));  // Simulate frame time
    }
    
    // Should still be ARMED (alignment sustained)
    ASSERT_EQ(sm.state(), aurore::FcsState::ARMED);
}

// ============================================================================
// Fire Command Tests
// ============================================================================

TEST(test_fire_command_from_armed) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    sm.set_interlock_enabled(true);
    
    sm.on_fire_command();
    
    // Should transition to IDLE_SAFE after fire
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_fire_command_from_tracking_rejected) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    sm.on_fire_command();
    
    // Should NOT transition (fire only from ARMED)
    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

TEST(test_fire_command_from_idle_safe_rejected) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::IDLE_SAFE);
    
    sm.on_fire_command();
    
    ASSERT_EQ(sm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(test_fire_command_without_interlock) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::ARMED);
    sm.set_interlock_enabled(false);
    
    sm.on_fire_command();
    
    // Should NOT transition (interlock not enabled)
    ASSERT_EQ(sm.state(), aurore::FcsState::ARMED);
}

// ============================================================================
// Redetection Score Tests
// ============================================================================

TEST(test_redetection_low_score_tracking) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    sm.on_redetection_score(0.70f);  // Below kRedetectionScoreMin (0.85)
    
    // Should transition to SEARCH
    ASSERT_EQ(sm.state(), aurore::FcsState::SEARCH);
}

TEST(test_redetection_high_score_tracking) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    sm.on_redetection_score(0.90f);  // Above threshold
    
    // Should stay in TRACKING
    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

// ============================================================================
// State Query Helper Tests
// ============================================================================

TEST(test_has_valid_lock) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    // Initially no valid lock
    ASSERT_FALSE(sm.has_valid_lock());
    
    // Set good redetection score
    sm.on_redetection_score(0.90f);
    ASSERT_TRUE(sm.has_valid_lock());
}

TEST(test_has_zero_faults) {
    aurore::StateMachine sm;
    sm.force_state_for_test(aurore::FcsState::TRACKING);
    
    // Initially no faults
    ASSERT_TRUE(sm.has_zero_faults());
    
    // Trigger fault
    sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    ASSERT_FALSE(sm.has_zero_faults());
}

TEST(test_has_operator_authorization) {
    aurore::StateMachine sm;
    
    // Initially not authorized
    ASSERT_FALSE(sm.has_operator_authorization());
    
    sm.set_operator_authorization(true);
    ASSERT_TRUE(sm.has_operator_authorization());
}

// ============================================================================
// State Name Tests
// ============================================================================

TEST(test_state_names) {
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::BOOT)), "BOOT");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::IDLE_SAFE)), "IDLE_SAFE");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::FREECAM)), "FREECAM");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::SEARCH)), "SEARCH");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::TRACKING)), "TRACKING");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::ARMED)), "ARMED");
    ASSERT_EQ(std::string(aurore::fcs_state_name(aurore::FcsState::FAULT)), "FAULT");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "Running State Machine Transition tests..." << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // AM7-L3-MODE-009: Invalid ARMED transition tests
    RUN_TEST(test_armed_only_from_tracking);
    RUN_TEST(test_armed_rejected_from_freecam);
    RUN_TEST(test_armed_rejected_from_search);
    RUN_TEST(test_armed_allowed_from_tracking);
    
    // FAULT state latching tests
    RUN_TEST(test_fault_latching_from_idle_safe);
    RUN_TEST(test_fault_latching_from_tracking);
    RUN_TEST(test_fault_latching_from_armed);
    RUN_TEST(test_fault_latching_from_search);
    RUN_TEST(test_fault_latching_from_freecam);
    RUN_TEST(test_fault_latching_interlock_inhibit);
    RUN_TEST(test_fault_latched_persistent);
    
    // SEARCH timeout tests
    RUN_TEST(test_search_timeout_transition);
    RUN_TEST(test_search_no_timeout_before_limit);
    RUN_TEST(test_search_detection_before_timeout);
    
    // ARMED timeout tests
    RUN_TEST(test_armed_timeout_transition);
    RUN_TEST(test_armed_no_timeout_before_limit);
    
    // Tracker update tests
    RUN_TEST(test_tracker_update_invalid_solution);
    RUN_TEST(test_tracker_update_low_psr);
    RUN_TEST(test_tracker_update_valid_solution);
    
    // Gimbal status tests
    RUN_TEST(test_gimbal_status_settled);
    RUN_TEST(test_gimbal_status_unsettled);
    RUN_TEST(test_gimbal_status_armed_alignment);
    
    // Fire command tests
    RUN_TEST(test_fire_command_from_armed);
    RUN_TEST(test_fire_command_from_tracking_rejected);
    RUN_TEST(test_fire_command_from_idle_safe_rejected);
    RUN_TEST(test_fire_command_without_interlock);
    
    // Redetection score tests
    RUN_TEST(test_redetection_low_score_tracking);
    RUN_TEST(test_redetection_high_score_tracking);
    
    // State query helper tests
    RUN_TEST(test_has_valid_lock);
    RUN_TEST(test_has_zero_faults);
    RUN_TEST(test_has_operator_authorization);
    
    // State name tests
    RUN_TEST(test_state_names);
    
    // Summary
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;
    
    return g_tests_failed.load() > 0 ? 1 : 0;
}
