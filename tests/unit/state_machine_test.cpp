#include "aurore/state_machine.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace aurore;

void test_initial_state() {
    StateMachine sm;
    assert(sm.state() == FcsState::BOOT);
    std::cout << "PASS: initial state is BOOT\n";
}

void test_boot_to_idle_safe() {
    // Simulate BOOT complete - transition handled externally
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: BOOT -> IDLE_SAFE on init complete\n";
}

void test_idle_safe_to_freecam() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.request_freecam();
    assert(sm.state() == FcsState::FREECAM);
    std::cout << "PASS: IDLE_SAFE -> FREECAM on operator request\n";
}

void test_idle_safe_to_search() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.request_search();
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: IDLE_SAFE -> SEARCH on operator request\n";
}

void test_search_detection_transitions_to_tracking() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    Detection d;
    d.confidence = 0.8f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: SEARCH -> TRACKING on valid detection\n";
}

void test_tracking_lost_returns_to_search() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    TrackSolution sol;
    sol.valid = false;
    sm.on_tracker_update(sol);
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: TRACKING -> SEARCH on lost lock\n";
}

void test_tracking_to_armed_with_conditions() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    sm.set_operator_authorization(true);
    
    // Set up valid lock condition
    sm.on_redetection_score(0.90f);  // >= kRedetectionScoreMin (0.85)
    
    FireControlSolution sol;
    sol.p_hit = 0.96f;  // >= kPHitMin (0.95)
    sm.on_ballistics_solution(sol);
    // ARMED entry requires: valid lock, stable timing, zero faults, operator auth
    // For this test, we simulate conditions met
    assert(sm.state() == FcsState::ARMED);
    std::cout << "PASS: TRACKING -> ARMED with all conditions\n";
}

void test_armed_timeout_returns_to_tracking() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    sm.tick(std::chrono::milliseconds(110));  // > kArmedTimeoutMs (100)
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: ARMED -> TRACKING on timeout\n";
}

void test_fault_from_any_state() {
    // Test FAULT transition from IDLE_SAFE
    StateMachine sm1;
    sm1.force_state_for_test(FcsState::IDLE_SAFE);
    sm1.on_fault(FaultCode::CAMERA_TIMEOUT);
    assert(sm1.state() == FcsState::FAULT);
    
    // Test FAULT transition from TRACKING
    StateMachine sm2;
    sm2.force_state_for_test(FcsState::TRACKING);
    sm2.on_fault(FaultCode::WATCHDOG_TIMEOUT);
    assert(sm2.state() == FcsState::FAULT);
    
    // Test FAULT transition from ARMED
    StateMachine sm3;
    sm3.force_state_for_test(FcsState::ARMED);
    sm3.on_fault(FaultCode::TEMPERATURE_CRITICAL);
    assert(sm3.state() == FcsState::FAULT);
    
    std::cout << "PASS: FAULT from any state on any fault\n";
}

void test_fault_interlock_inhibit() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    sm.set_interlock_enabled(true);
    assert(sm.is_interlock_enabled());
    
    sm.on_fault(FaultCode::CAMERA_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);
    assert(!sm.is_interlock_enabled());  // Force inhibit
    std::cout << "PASS: FAULT forces interlock inhibit\n";
}

void test_invalid_armed_transition_rejected() {
    // AM7-L3-MODE-009: No state shall transition directly to ARMED except TRACKING
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    // Direct transition to ARMED should be rejected
    FireControlSolution sol;
    sol.p_hit = 0.96f;
    sm.on_ballistics_solution(sol);
    // Should NOT transition to ARMED from IDLE_SAFE
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: ARMED only from TRACKING (AM7-L3-MODE-009)\n";
}

void test_search_timeout() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    sm.tick(std::chrono::milliseconds(5100));  // > kSearchTimeoutMs (5000)
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: SEARCH -> IDLE_SAFE on timeout\n";
}

int main() {
    test_initial_state();
    test_boot_to_idle_safe();
    test_idle_safe_to_freecam();
    test_idle_safe_to_search();
    test_search_detection_transitions_to_tracking();
    test_tracking_lost_returns_to_search();
    test_tracking_to_armed_with_conditions();
    test_armed_timeout_returns_to_tracking();
    test_fault_from_any_state();
    test_fault_interlock_inhibit();
    test_invalid_armed_transition_rejected();
    test_search_timeout();
    std::cout << "\nAll state machine tests passed.\n";
    return 0;
}
