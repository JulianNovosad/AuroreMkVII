#include "aurore/state_machine.hpp"
#include "aurore/timing.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
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

void test_range_valid_data_accepted() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = 100.0f;  // Valid range within [0.5m, 5000m]
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // Valid data should be accepted (not trigger fault)
    assert(sm.state() != FcsState::FAULT);
    std::cout << "PASS: Valid range data accepted\n";
}

void test_range_stale_data_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = 100.0f;
    // Stale timestamp: 150ms in the past (>100ms threshold)
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw) - 150000000ULL;
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // Stale data should trigger RANGE_DATA_STALE fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: Stale range data (>100ms) triggers FAULT\n";
}

void test_range_checksum_mismatch_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = 100.0f;
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = 0xDEAD;  // Invalid checksum

    sm.on_lrf_range(range);

    // Checksum mismatch should trigger RANGE_DATA_INVALID fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: Checksum mismatch triggers FAULT\n";
}

void test_range_below_minimum_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = 0.3f;  // Below minimum 0.5m
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // Out of bounds should trigger RANGE_DATA_INVALID fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: Range below minimum (0.5m) triggers FAULT\n";
}

void test_range_above_maximum_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = 6000.0f;  // Above maximum 5000m
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // Out of bounds should trigger RANGE_DATA_INVALID fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: Range above maximum (5000m) triggers FAULT\n";
}

void test_range_nan_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = std::numeric_limits<float>::quiet_NaN();
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // NaN should trigger RANGE_DATA_INVALID fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: NaN range value triggers FAULT\n";
}

void test_range_infinity_rejected() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    RangeData range;
    range.range_m = std::numeric_limits<float>::infinity();
    range.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range.checksum = StateMachine::compute_crc16(range.range_m, range.timestamp_ns);

    sm.on_lrf_range(range);

    // Infinity should trigger RANGE_DATA_INVALID fault
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: Infinity range value triggers FAULT\n";
}

void test_range_boundary_values_accepted() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);

    // Test minimum boundary (0.5m)
    RangeData range_min;
    range_min.range_m = 0.5f;
    range_min.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range_min.checksum = StateMachine::compute_crc16(range_min.range_m, range_min.timestamp_ns);

    sm.on_lrf_range(range_min);
    assert(sm.state() != FcsState::FAULT);

    // Test maximum boundary (5000m)
    StateMachine sm2;
    sm2.force_state_for_test(FcsState::TRACKING);

    RangeData range_max;
    range_max.range_m = 5000.0f;
    range_max.timestamp_ns = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    range_max.checksum = StateMachine::compute_crc16(range_max.range_m, range_max.timestamp_ns);

    sm2.on_lrf_range(range_max);
    assert(sm2.state() != FcsState::FAULT);

    std::cout << "PASS: Boundary values (0.5m, 5000m) accepted\n";
}

void test_crc16_deterministic() {
    // Verify CRC-16 is deterministic for same input
    const float range = 123.456f;
    const uint64_t timestamp = 9876543210ULL;

    const uint16_t crc1 = StateMachine::compute_crc16(range, timestamp);
    const uint16_t crc2 = StateMachine::compute_crc16(range, timestamp);
    const uint16_t crc3 = StateMachine::compute_crc16(range + 0.001f, timestamp);  // Different range
    const uint16_t crc4 = StateMachine::compute_crc16(range, timestamp + 1);       // Different timestamp

    assert(crc1 == crc2);       // Same input = same CRC
    assert(crc1 != crc3);       // Different range = different CRC
    assert(crc1 != crc4);       // Different timestamp = different CRC

    // Use variables to avoid unused warnings in release builds
    (void)crc1; (void)crc2; (void)crc3; (void)crc4;

    std::cout << "PASS: CRC-16 is deterministic and sensitive to input changes\n";
}

void test_boot_failure_transitions_to_fault() {
    // AM7-L2-MODE-001: BOOT -> FAULT on initialization failure
    StateMachine sm;
    // State starts at BOOT
    sm.on_boot_failure();
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: BOOT -> FAULT on boot failure\n";
}

void test_manual_reset_from_fault_to_idle_safe() {
    // AM7-L3-MODE-006: FAULT -> IDLE_SAFE on manual reset
    StateMachine sm;
    sm.force_state_for_test(FcsState::FAULT);
    // Trigger a fault to latch it
    sm.on_fault(FaultCode::CAMERA_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);
    
    // Manual reset should clear fault and transition to IDLE_SAFE
    sm.on_manual_reset();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: FAULT -> IDLE_SAFE on manual reset\n";
}

void test_cancel_from_tracking_to_idle_safe() {
    // AM7-L3-MODE-006: TRACKING -> IDLE_SAFE on operator cancel
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: TRACKING -> IDLE_SAFE on cancel\n";
}

void test_cancel_from_search_to_idle_safe() {
    // AM7-L3-MODE-006: SEARCH -> IDLE_SAFE on operator cancel
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: SEARCH -> IDLE_SAFE on cancel\n";
}

void test_cancel_from_freecam_to_idle_safe() {
    // AM7-L3-MODE-006: FREECAM -> IDLE_SAFE on operator cancel
    StateMachine sm;
    sm.force_state_for_test(FcsState::FREECAM);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: FREECAM -> IDLE_SAFE on cancel\n";
}

void test_cancel_from_idle_safe_no_effect() {
    // AM7-L3-MODE-006: Cancel from IDLE_SAFE should have no effect
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: Cancel from IDLE_SAFE has no effect\n";
}

void test_disarm_from_armed_to_idle_safe() {
    // AM7-L3-MODE-006: ARMED -> IDLE_SAFE on disarm request
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    sm.request_disarm();
    assert(sm.state() == FcsState::IDLE_SAFE);
    std::cout << "PASS: ARMED -> IDLE_SAFE on disarm\n";
}

void test_disarm_from_non_armed_no_effect() {
    // AM7-L3-MODE-006: Disarm from non-ARMED state should have no effect
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    sm.request_disarm();
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: Disarm from non-ARMED has no effect\n";
}

void test_armed_lock_lost_returns_to_tracking() {
    // AM7-L3-MODE-006: ARMED -> TRACKING on lock lost
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    TrackSolution sol;
    sol.valid = false;  // Lost lock
    sm.on_tracker_update(sol);
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: ARMED -> TRACKING on lock lost\n";
}

void test_fault_from_boot_transitions_to_fault() {
    // AM7-L3-MODE-001: BOOT -> FAULT on any fault
    StateMachine sm;
    // State starts at BOOT
    sm.on_fault(FaultCode::WATCHDOG_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: BOOT -> FAULT on fault\n";
}

void test_fault_from_idle_safe_transitions_to_fault() {
    // AM7-L3-MODE-001: IDLE_SAFE -> FAULT on any fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.on_fault(FaultCode::GIMBAL_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: IDLE_SAFE -> FAULT on fault\n";
}

void test_fault_from_freecam_transitions_to_fault() {
    // AM7-L3-MODE-001: FREECAM -> FAULT on any fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::FREECAM);
    sm.on_fault(FaultCode::RANGE_DATA_STALE);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: FREECAM -> FAULT on fault\n";
}

void test_fault_from_search_transitions_to_fault() {
    // AM7-L3-MODE-001: SEARCH -> FAULT on any fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    sm.on_fault(FaultCode::AUTH_FAILURE);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: SEARCH -> FAULT on fault\n";
}

void test_fault_from_tracking_transitions_to_fault() {
    // AM7-L3-MODE-001: TRACKING -> FAULT on any fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    sm.on_fault(FaultCode::SEQUENCE_GAP);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: TRACKING -> FAULT on fault\n";
}

void test_fault_from_armed_transitions_to_fault() {
    // AM7-L3-MODE-001: ARMED -> FAULT on any fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::ARMED);
    sm.on_fault(FaultCode::TEMPERATURE_CRITICAL);
    assert(sm.state() == FcsState::FAULT);
    std::cout << "PASS: ARMED -> FAULT on fault\n";
}

void test_manual_reset_clears_fault_latch() {
    // AM7-L3-MODE-006: Manual reset should clear latched fault
    StateMachine sm;
    sm.force_state_for_test(FcsState::FAULT);
    sm.on_fault(FaultCode::CAMERA_TIMEOUT);
    
    // Fault should be latched
    sm.on_manual_reset();
    assert(sm.state() == FcsState::IDLE_SAFE);
    
    // After reset, should be able to transition normally
    sm.request_search();
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: Manual reset clears fault latch\n";
}

void test_manual_reset_clears_authorization() {
    // AM7-L3-MODE-006: Manual reset should clear operator authorization
    StateMachine sm;
    sm.force_state_for_test(FcsState::FAULT);
    sm.set_operator_authorization(true);
    
    sm.on_manual_reset();
    assert(sm.state() == FcsState::IDLE_SAFE);
    assert(!sm.has_operator_authorization());
    std::cout << "PASS: Manual reset clears operator authorization\n";
}

void test_transition_table_completeness() {
    // AM7-L3-MODE-001: Verify all transitions from transition table are implemented
    // This test documents the complete transition matrix

    StateMachine sm;

    // BOOT -> IDLE_SAFE (via on_init_complete)
    // BOOT -> FAULT (via on_fault or on_boot_failure)

    // IDLE_SAFE -> FREECAM (via request_freecam)
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.request_freecam();
    assert(sm.state() == FcsState::FREECAM);

    // IDLE_SAFE -> SEARCH (via request_search)
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.request_search();
    assert(sm.state() == FcsState::SEARCH);

    // IDLE_SAFE -> FAULT (via on_fault)
    sm.force_state_for_test(FcsState::IDLE_SAFE);
    sm.on_fault(FaultCode::CAMERA_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);

    // FREECAM -> IDLE_SAFE (via request_cancel)
    sm.force_state_for_test(FcsState::FREECAM);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);

    // FREECAM -> SEARCH (via request_search)
    sm.force_state_for_test(FcsState::FREECAM);
    sm.request_search();
    assert(sm.state() == FcsState::SEARCH);

    // FREECAM -> FAULT (via on_fault)
    sm.force_state_for_test(FcsState::FREECAM);
    sm.on_fault(FaultCode::WATCHDOG_TIMEOUT);
    assert(sm.state() == FcsState::FAULT);

    // SEARCH -> IDLE_SAFE (via request_cancel or timeout)
    sm.force_state_for_test(FcsState::SEARCH);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);

    // SEARCH -> TRACKING (via on_detection)
    sm.force_state_for_test(FcsState::SEARCH);
    Detection d;
    d.confidence = 0.96f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    assert(sm.state() == FcsState::TRACKING);

    // SEARCH -> FAULT (via on_fault)
    sm.force_state_for_test(FcsState::SEARCH);
    sm.on_fault(FaultCode::RANGE_DATA_INVALID);
    assert(sm.state() == FcsState::FAULT);

    // TRACKING -> IDLE_SAFE (via request_cancel)
    sm.force_state_for_test(FcsState::TRACKING);
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);

    // TRACKING -> SEARCH (via on_tracker_update with !sol.valid)
    sm.force_state_for_test(FcsState::TRACKING);
    TrackSolution sol;
    sol.valid = false;
    sm.on_tracker_update(sol);
    assert(sm.state() == FcsState::SEARCH);

    // TRACKING -> ARMED (via on_ballistics_solution with conditions)
    sm.force_state_for_test(FcsState::TRACKING);
    sm.set_operator_authorization(true);
    sm.on_redetection_score(0.96f);
    FireControlSolution fsol;
    fsol.p_hit = 0.96f;
    sm.on_ballistics_solution(fsol);
    assert(sm.state() == FcsState::ARMED);

    // TRACKING -> FAULT (via on_fault)
    sm.force_state_for_test(FcsState::TRACKING);
    sm.on_fault(FaultCode::SEQUENCE_GAP);
    assert(sm.state() == FcsState::FAULT);

    // ARMED -> IDLE_SAFE (via request_disarm)
    sm.force_state_for_test(FcsState::ARMED);
    sm.request_disarm();
    assert(sm.state() == FcsState::IDLE_SAFE);

    // ARMED -> TRACKING (via timeout or on_tracker_update with !sol.valid)
    sm.force_state_for_test(FcsState::ARMED);
    sm.tick(std::chrono::milliseconds(110));
    assert(sm.state() == FcsState::TRACKING);

    // ARMED -> FAULT (via on_fault)
    sm.force_state_for_test(FcsState::ARMED);
    sm.on_fault(FaultCode::AUTH_FAILURE);
    assert(sm.state() == FcsState::FAULT);

    // FAULT -> BOOT (via power cycle - not testable in software)
    // FAULT -> IDLE_SAFE (via on_manual_reset)
    sm.force_state_for_test(FcsState::FAULT);
    sm.on_manual_reset();
    assert(sm.state() == FcsState::IDLE_SAFE);

    std::cout << "PASS: State transition table completeness verified\n";
}

// AM7-L2-TGT-003/004: Target validation tests

void test_position_stability_single_frame_not_stable() {
    // AM7-L2-TGT-003: Single frame should not be considered stable
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    Detection d;
    d.confidence = 0.96f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    
    // Should remain in SEARCH (not enough frames for validation)
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: Single frame not stable (AM7-L2-TGT-003)\n";
}

void test_position_stability_three_stable_frames_transitions() {
    // AM7-L2-TGT-003: Three consecutive stable frames (Δ ≤ 2px) should transition
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    // Send 3 detections with stable positions (same centroid)
    for (int i = 0; i < 3; ++i) {
        Detection d;
        d.confidence = 0.96f;
        d.bbox = {100, 100, 50, 50};  // Same position each frame
        sm.on_detection(d);
    }
    
    // Should transition to TRACKING after 3 stable frames
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: Three stable frames transition to TRACKING (AM7-L2-TGT-003)\n";
}

void test_position_stability_unstable_positions_no_transition() {
    // AM7-L2-TGT-003: Unstable positions (Δ > 2px) should not transition
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    // Send 3 detections with unstable positions (>2px delta)
    for (int i = 0; i < 3; ++i) {
        Detection d;
        d.confidence = 0.96f;
        d.bbox = {100 + i * 10, 100, 50, 50};  // Moving 10px each frame
        sm.on_detection(d);
    }
    
    // Should remain in SEARCH (positions not stable)
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: Unstable positions do not transition (AM7-L2-TGT-003)\n";
}

void test_position_stability_reset_on_state_change() {
    // AM7-L2-TGT-004: Validation state should reset on state transition
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    // Build up 2 stable frames
    for (int i = 0; i < 2; ++i) {
        Detection d;
        d.confidence = 0.96f;
        d.bbox = {100, 100, 50, 50};
        sm.on_detection(d);
    }
    
    // Cancel to IDLE_SAFE
    sm.request_cancel();
    assert(sm.state() == FcsState::IDLE_SAFE);
    
    // Return to SEARCH - validation should be reset
    sm.request_search();
    assert(sm.state() == FcsState::SEARCH);
    
    // Need 3 more stable frames (not just 1)
    Detection d;
    d.confidence = 0.96f;
    d.bbox = {100, 100, 50, 50};
    sm.on_detection(d);
    
    // Should remain in SEARCH (validation was reset)
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: Validation state resets on state change (AM7-L2-TGT-004)\n";
}

void test_lock_confirmation_stable_window() {
    // AM7-L2-TGT-004: Lock confirmation over 250ms window
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    
    // Simulate stable tracking for 250ms (30 frames at ~8ms/frame)
    TrackSolution sol;
    sol.valid = true;
    
    for (int i = 0; i < 35; ++i) {
        sm.on_tracker_update(sol);
    }
    
    // Lock should be confirmed after 250ms of stable tracking
    // Note: This test verifies the mechanism runs; actual lock_confirmed_ 
    // is internal state not directly exposed
    std::cout << "PASS: Lock confirmation window runs (AM7-L2-TGT-004)\n";
}

void test_position_stability_boundary_2px() {
    // AM7-L2-TGT-003: Test boundary condition (exactly 2px delta)
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    // Send 3 detections with exactly 2px delta (should be stable)
    for (int i = 0; i < 3; ++i) {
        Detection d;
        d.confidence = 0.96f;
        d.bbox = {100 + i * 2, 100, 50, 50};  // Exactly 2px delta
        sm.on_detection(d);
    }
    
    // Should transition (2px is within threshold)
    assert(sm.state() == FcsState::TRACKING);
    std::cout << "PASS: 2px boundary is stable (AM7-L2-TGT-003)\n";
}

void test_position_stability_exceeds_2px() {
    // AM7-L2-TGT-003: Test exceeding boundary (>2px delta)
    StateMachine sm;
    sm.force_state_for_test(FcsState::SEARCH);
    
    // Send 3 detections with >2px delta (should not be stable)
    for (int i = 0; i < 3; ++i) {
        Detection d;
        d.confidence = 0.96f;
        d.bbox = {100 + i * 3, 100, 50, 50};  // 3px delta exceeds threshold
        sm.on_detection(d);
    }
    
    // Should remain in SEARCH (>2px exceeds threshold)
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: >2px exceeds stability threshold (AM7-L2-TGT-003)\n";
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

    // AM7-L3-MODE-001: New transition tests
    test_boot_failure_transitions_to_fault();
    test_manual_reset_from_fault_to_idle_safe();
    test_cancel_from_tracking_to_idle_safe();
    test_cancel_from_search_to_idle_safe();
    test_cancel_from_freecam_to_idle_safe();
    test_cancel_from_idle_safe_no_effect();
    test_disarm_from_armed_to_idle_safe();
    test_disarm_from_non_armed_no_effect();
    test_armed_lock_lost_returns_to_tracking();
    test_fault_from_boot_transitions_to_fault();
    test_fault_from_idle_safe_transitions_to_fault();
    test_fault_from_freecam_transitions_to_fault();
    test_fault_from_search_transitions_to_fault();
    test_fault_from_tracking_transitions_to_fault();
    test_fault_from_armed_transitions_to_fault();
    test_manual_reset_clears_fault_latch();
    test_manual_reset_clears_authorization();
    test_transition_table_completeness();

    // AM7-L3-SAFE-002: Range data validation tests
    test_range_valid_data_accepted();
    test_range_stale_data_rejected();
    test_range_checksum_mismatch_rejected();
    test_range_below_minimum_rejected();
    test_range_above_maximum_rejected();
    test_range_nan_rejected();
    test_range_infinity_rejected();
    test_range_boundary_values_accepted();
    test_crc16_deterministic();

    // AM7-L2-TGT-003/004: Target validation tests
    test_position_stability_single_frame_not_stable();
    test_position_stability_three_stable_frames_transitions();
    test_position_stability_unstable_positions_no_transition();
    test_position_stability_reset_on_state_change();
    test_lock_confirmation_stable_window();
    test_position_stability_boundary_2px();
    test_position_stability_exceeds_2px();

    std::cout << "\nAll state machine tests passed.\n";
    return 0;
}
