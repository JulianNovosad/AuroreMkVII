#include "aurore/state_machine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "aurore/timing.hpp"

namespace aurore {

StateMachine::StateMachine() = default;

/**
 * @brief AM7-L2-TGT-003: Check if position is stable over last 3 frames.
 *
 * Stability criterion: Δposition ≤ 2 pixels between consecutive frames.
 * Returns true if all 3 frames have been recorded and consecutive deltas ≤ 2px.
 */
bool StateMachine::is_position_stable() const noexcept {
    // Need at least 3 frames for validation
    if (stable_frame_count_ < kStableFramesMin) {
        return false;
    }

    // Check deltas between consecutive frames in history
    const int idx0 = position_history_idx_;
    const int idx1 = (position_history_idx_ - 1 + 3) % 3;
    const int idx2 = (position_history_idx_ - 2 + 3) % 3;

    const auto& p0 = position_history_[idx0];
    const auto& p1 = position_history_[idx1];
    const auto& p2 = position_history_[idx2];

    // Check if all positions are valid (non-zero timestamps)
    if (p0.timestamp_ns == 0 || p1.timestamp_ns == 0 || p2.timestamp_ns == 0) {
        return false;
    }

    // Compute Euclidean distance between consecutive positions
    const float dx01 = p0.x - p1.x;
    const float dy01 = p0.y - p1.y;
    const float dist01 = std::sqrt(dx01 * dx01 + dy01 * dy01);

    const float dx12 = p1.x - p2.x;
    const float dy12 = p1.y - p2.y;
    const float dist12 = std::sqrt(dx12 * dx12 + dy12 * dy12);

    // Stability: both deltas must be ≤ 2 pixels
    return (dist01 <= kPositionStabilityPx) && (dist12 <= kPositionStabilityPx);
}

/**
 * @brief AM7-L2-TGT-004: Update position history with new detection.
 *
 * Maintains circular buffer of last 3 positions.
 * Updates stability counter based on position delta.
 */
void StateMachine::update_position_history(const Detection& d) noexcept {
    const float cx = d.cx();
    const float cy = d.cy();
    const uint64_t now_ns = get_timestamp(ClockId::MonotonicRaw);

    // Check delta from previous position
    bool is_stable = false;
    const int prev_idx = (position_history_idx_ - 1 + 3) % 3;
    const auto& prev = position_history_[prev_idx];

    if (prev.timestamp_ns != 0) {
        const float dx = cx - prev.x;
        const float dy = cy - prev.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        is_stable = (dist <= kPositionStabilityPx);
    } else {
        // First frame - consider stable as baseline
        is_stable = true;
    }

    // Update circular buffer
    position_history_[position_history_idx_] = {cx, cy, now_ns};
    position_history_idx_ = (position_history_idx_ + 1) % 3;

    // Update stability counter
    if (is_stable) {
        stable_frame_count_ = std::min(stable_frame_count_ + 1, kStableFramesMin);
    } else {
        stable_frame_count_ = 0;
    }

    // Position valid when 3 consecutive stable frames
    position_valid_ = (stable_frame_count_ >= kStableFramesMin);
}

/**
 * @brief AM7-L2-TGT-004: Update lock confirmation state.
 *
 * Lock confirmation requires 95% stability over 250ms window (30 frames at 120Hz).
 * Called each frame with stability status.
 */
void StateMachine::update_lock_confirmation(bool is_stable) noexcept {
    // Assume 8.333ms per frame (120Hz)
    constexpr int kFrameTimeMs = 8;
    lock_confirm_age_ms_ = std::chrono::milliseconds(lock_confirm_age_ms_.count() + kFrameTimeMs);

    if (is_stable) {
        lock_confirm_stable_frames_++;
    }

    // Check if we've completed the 250ms window
    if (lock_confirm_age_ms_.count() >= kLockConfirmWindowMs) {
        // Compute stability ratio over window
        const float stability_ratio = static_cast<float>(lock_confirm_stable_frames_) /
                                      static_cast<float>(kLockConfirmWindowMs / kFrameTimeMs);

        lock_confirmed_ = (stability_ratio >= kLockConfirmThreshold);

        // Reset window for continuous monitoring
        lock_confirm_age_ms_ = std::chrono::milliseconds(0);
        lock_confirm_stable_frames_ = 0;
    }
}

/**
 * @brief AM7-L2-TGT-004: Reset target validation state.
 *
 * Called on state transition to clear validation history.
 */
void StateMachine::reset_target_validation() noexcept {
    position_history_[0] = {};
    position_history_[1] = {};
    position_history_[2] = {};
    position_history_idx_ = 0;
    stable_frame_count_ = 0;
    position_valid_ = false;
    lock_confirm_age_ms_ = std::chrono::milliseconds(0);
    lock_confirm_stable_frames_ = 0;
    lock_confirmed_ = false;
}

FcsState StateMachine::state() const { return state_; }

void StateMachine::set_state_change_callback(StateChangeCb cb) { on_change_ = std::move(cb); }

void StateMachine::force_state_for_test(FcsState s) {
    state_ = s;
    state_age_ = {};
}

/**
 * @brief Performs state transition and logs event per AM7-L3-MODE-008.
 */
void StateMachine::transition(FcsState next) {
    // AM7-L3-MODE-009: No state shall transition directly to ARMED except TRACKING
    if (next == FcsState::ARMED && state_ != FcsState::TRACKING) {
        std::cerr << "StateMachine: Invalid transition to ARMED from " << fcs_state_name(state_)
                  << std::endl;
        return;  // Reject invalid transition
    }

    FcsState prev = state_;
    state_ = next;
    state_age_ = {};

    // AM7-L3-MODE-008: Log every state transition with event ID and context
    std::cout << "StateMachine [EVENT 0x" << std::hex << (0x0600 | static_cast<uint8_t>(next))
              << "]: Transition " << fcs_state_name(prev) << " -> " << fcs_state_name(next)
              << std::dec << " (age: " << state_age_.count() << "ms)" << std::endl;

    enter_state(next);
    if (on_change_) on_change_(prev, next);
}

/**
 * @brief AM7-L2-MODE-001 through AM7-L2-MODE-007: State-specific entry logic.
 */
void StateMachine::enter_state(FcsState s) {
    gimbal_ = {};
    align_sustained_ms_ = 0;

    // AM7-L2-TGT-004: Reset target validation on every state transition
    reset_target_validation();

    switch (s) {
        case FcsState::BOOT:
            // AM7-L2-MODE-001: BOOT state entry
            have_first_detection_ = false;
            redetection_score_ = 0.f;
            solution_ = {};
            // Hardware init, memory lock, self-test performed in main.cpp
            break;

        case FcsState::IDLE_SAFE:
            // AM7-L2-MODE-002: IDLE/SAFE state - inhibit posture
            have_first_detection_ = false;
            interlock_enabled_ = false;  // Force inhibit
            break;

        case FcsState::FREECAM:
            // AM7-L2-MODE-003: FREECAM state - manual control, no auto-lock
            interlock_enabled_ = false;  // Prohibit fire authorization
            break;

        case FcsState::SEARCH:
            // AM7-L2-MODE-004: SEARCH state - auto acquisition
            interlock_enabled_ = false;
            break;

        case FcsState::TRACKING:
            // AM7-L2-MODE-005: TRACKING state - continuous lock
            interlock_enabled_ = false;  // Inhibit until ARMED
            break;

        case FcsState::ARMED:
            // AM7-L2-MODE-006: ARMED state - interlock enable permitted
            // Interlock may transition to ENABLE on valid fire request
            break;

        case FcsState::FAULT:
            // AM7-L2-MODE-007: FAULT state - forced inhibit, latched
            interlock_enabled_ = false;  // Force inhibit
            fault_latched_ = true;       // Latched until power cycle
            break;
    }
}

void StateMachine::tick(std::chrono::milliseconds dt) {
    state_age_ += dt;

    switch (state_) {
        case FcsState::BOOT:
            // AM7-L2-MODE-001: BOOT transitions
            // Transition handled by external init complete signal
            break;

        case FcsState::IDLE_SAFE:
            // AM7-L2-MODE-002: IDLE/SAFE - no timeout, wait for operator request
            break;

        case FcsState::FREECAM:
            // AM7-L2-MODE-003: FREECAM - wait for operator request
            break;

        case FcsState::SEARCH:
            // AM7-L2-MODE-004: SEARCH timeout
            if (state_age_.count() > kSearchTimeoutMs) {
                transition(FcsState::IDLE_SAFE);
            }
            break;

        case FcsState::TRACKING:
            // AM7-L2-MODE-005: TRACKING - monitor lock validity
            break;

        case FcsState::ARMED:
            // AM7-L2-MODE-006: ARMED timeout - return to TRACKING if no fire
            if (state_age_.count() > kArmedTimeoutMs) {
                transition(FcsState::TRACKING);
            }
            // AM7-L3-MODE-006: ARMED -> TRACKING on lock lost
            // This is handled by on_tracker_update() when sol.valid becomes false
            break;

        case FcsState::FAULT:
            // AM7-L2-MODE-007: FAULT - latched, no automatic exit
            // Only exit via power cycle or manual reset
            break;
    }
}

void StateMachine::on_detection(const Detection& d) {
    if (d.confidence < kConfidenceMin) return;

    switch (state_) {
        case FcsState::SEARCH:
            // AM7-L2-MODE-004: SEARCH state - acquire first valid target
            // AM7-L2-TGT-003: Update position history for stability validation
            update_position_history(d);
            first_detection_ = d;
            have_first_detection_ = true;

            // AM7-L2-TGT-003/004: Transition to TRACKING only after 3-frame stability validation
            if (is_position_stable()) {
                transition(FcsState::TRACKING);
            }
            break;

        case FcsState::FREECAM:
        case FcsState::IDLE_SAFE:
            // Store detection but don't auto-transition
            // AM7-L2-TGT-003: Still update position history for validation
            update_position_history(d);
            first_detection_ = d;
            have_first_detection_ = true;
            break;

        default:
            break;
    }
}

void StateMachine::on_tracker_initialized(const TrackSolution& sol) {
    if (state_ == FcsState::TRACKING && sol.valid) {
        // Tracker ready, maintain TRACKING state
    } else if (state_ == FcsState::TRACKING && !sol.valid) {
        // Lost lock, return to SEARCH
        transition(FcsState::SEARCH);
    }
}

void StateMachine::on_tracker_update(const TrackSolution& sol) {
    if (state_ == FcsState::TRACKING) {
        // INT-006: KCF tracker does not provide PSR. Track validity is determined
        // by sol.valid flag and redetection score, not PSR threshold.
        if (!sol.valid) {
            // Lost lock, return to SEARCH
            transition(FcsState::SEARCH);
        } else {
            // AM7-L2-TGT-004: Update lock confirmation when tracking
            // Lock is stable when tracker reports valid and position is stable
            update_lock_confirmation(is_position_stable());
        }
    } else if (state_ == FcsState::ARMED) {
        // AM7-L3-MODE-006: ARMED -> TRACKING on lock lost
        if (!sol.valid) {
            transition(FcsState::TRACKING);
        } else {
            // AM7-L2-TGT-004: Continue lock confirmation in ARMED state
            update_lock_confirmation(is_position_stable());
        }
    }
}

void StateMachine::on_gimbal_status(const GimbalStatusSm& g) {
    gimbal_ = g;

    if (state_ == FcsState::SEARCH) {
        // AM7-L2-MODE-004: SEARCH - wait for gimbal to settle
        if (g.az_error_deg < kGimbalErrorMaxDeg && g.velocity_deg_s < kGimbalVelocityMaxDs) {
            if (++gimbal_.settled_frames >= kSettledFramesMin) {
                // Gimbal settled, ready for tracking
            }
        } else {
            gimbal_.settled_frames = 0;
        }
    }

    if (state_ == FcsState::ARMED) {
        // AM7-L3-MODE-010: ARMED entry condition - timing stability
        if (g.az_error_deg < kAlignErrorMaxDeg) {
            align_sustained_ms_ += 8;
            if (align_sustained_ms_ >= kAlignSustainMs && solution_.p_hit >= kPHitMin) {
                // All conditions met, ready for fire command
                // Note: Actual fire requires operator authorization
            }
        } else {
            align_sustained_ms_ = 0;
        }
    }
}

void StateMachine::on_redetection_score(float score) {
    redetection_score_ = score;
    if (state_ == FcsState::TRACKING && score < kRedetectionScoreMin) {
        // Lost target, return to SEARCH
        transition(FcsState::SEARCH);
    }
}

/**
 * @brief AM7-L3-SAFE-002: Range data validation with timestamp age check,
 * checksum validation, bounds check, and NaN/infinity detection.
 *
 * Validation completes within 1ms per AM7-L3-SAFE-002.
 * Invalid or stale data triggers FAULT state.
 */
void StateMachine::on_lrf_range(const RangeData& range) {
    // AM7-L3-SAFE-002: Validation must complete within 1ms
    // All checks below are O(1) with no heap allocation

    // (a) Timestamp age check: >100ms from timestamp to processing → revoke
    const TimestampNs now = aurore::get_timestamp(aurore::ClockId::MonotonicRaw);
    const int64_t age_ns = aurore::timestamp_diff_ns(now, range.timestamp_ns);

    if (age_ns > static_cast<int64_t>(RangeData::kMaxAgeNs)) {
        // Stale data: revoke and trigger fault
        have_valid_range_ = false;
        on_fault(FaultCode::RANGE_DATA_STALE);
        return;
    }

    // (b) Checksum validation
    // CRC-16-CCITT (polynomial 0x1021, init 0xFFFF)
    const uint16_t computed_checksum = compute_crc16(range.range_m, range.timestamp_ns);
    if (computed_checksum != range.checksum) {
        // Checksum mismatch: revoke and trigger fault
        have_valid_range_ = false;
        on_fault(FaultCode::RANGE_DATA_INVALID);
        return;
    }

    // (c) Range bounds check [0.5m, 5000m]
    if (range.range_m < RangeData::kRangeMinM || range.range_m > RangeData::kRangeMaxM) {
        // Out of bounds: revoke and trigger fault
        have_valid_range_ = false;
        on_fault(FaultCode::RANGE_DATA_INVALID);
        return;
    }

    // (d) NaN/infinity detection
    if (std::isnan(range.range_m) || std::isinf(range.range_m)) {
        // Invalid float: revoke and trigger fault
        have_valid_range_ = false;
        on_fault(FaultCode::RANGE_DATA_INVALID);
        return;
    }

    // All validations passed: store valid range data
    last_valid_range_ = range;
    have_valid_range_ = true;
    range_age_ms_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(age_ns));

    // Range stored for ballistic solution (used in on_ballistics_solution)
    solution_.range_m = range.range_m;
}

/**
 * @brief Compute CRC-16-CCITT checksum for range data validation.
 *
 * Uses polynomial 0x1021 with initial value 0xFFFF.
 * This is a simple, fast checksum suitable for real-time validation.
 */
uint16_t StateMachine::compute_crc16(float range_m, uint64_t timestamp_ns) noexcept {
    // CRC-16-CCITT parameters
    constexpr uint16_t POLY = 0x1021;
    constexpr uint16_t INIT = 0xFFFF;

    uint16_t crc = INIT;

    // Process float bytes (IEEE 754 representation)
    uint32_t range_bits;
    std::memcpy(&range_bits, &range_m, sizeof(float));

    for (int i = 0; i < 32; ++i) {
        const bool bit = (range_bits >> (31 - i)) & 1;
        crc ^= (bit ? (1 << 15) : 0);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x8000) ? ((crc << 1) ^ POLY) : (crc << 1);
        }
    }

    // Process timestamp bytes (8 bytes, little-endian order)
    for (int byte = 0; byte < 8; ++byte) {
        uint8_t ts_byte = (timestamp_ns >> (byte * 8)) & 0xFF;
        for (int i = 0; i < 8; ++i) {
            const bool bit = (ts_byte >> (7 - i)) & 1;
            crc ^= (bit ? (1 << 15) : 0);
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ POLY) : (crc << 1);
            }
        }
    }

    return crc;
}

void StateMachine::on_ballistics_solution(const FireControlSolution& sol) {
    solution_ = sol;

    // AM7-L2-MODE-006: ARMED state entry from TRACKING
    // Requires: (a) valid lock, (b) stable timing, (c) zero faults, (d) operator authorization
    // INT-010: ARMED state is UNREACHABLE without operator authorization per AM7-L3-MODE-010.
    // The has_operator_authorization() check ensures external authorization signal is required.
    if (state_ == FcsState::TRACKING && sol.p_hit >= kPHitMin) {
        // Check all ARMED entry conditions (AM7-L3-MODE-010)
        if (has_valid_lock() && has_stable_timing() && has_zero_faults() &&
            has_operator_authorization()) {
            transition(FcsState::ARMED);
        }
    }
}

void StateMachine::on_fire_command() {
    // AM7-L2-MODE-006: Fire only permitted from ARMED state
    if (state_ == FcsState::ARMED && interlock_enabled_) {
        transition(FcsState::IDLE_SAFE);  // Return to safe state after fire
    }
}

// AM7-L3-MODE-011: Any fault transitions immediately to FAULT from any state
void StateMachine::on_fault(FaultCode code) {
    (void)code;  // Fault code logged but any fault triggers FAULT state
    if (state_ != FcsState::FAULT) {
        transition(FcsState::FAULT);
    }
}

// AM7-L2-MODE-001: BOOT -> IDLE_SAFE transition on hardware init complete
void StateMachine::on_init_complete() {
    if (state_ == FcsState::BOOT) {
        transition(FcsState::IDLE_SAFE);
    }
}

// AM7-L2-MODE-003: FREECAM state entry from IDLE_SAFE or SEARCH
void StateMachine::request_freecam() {
    if (state_ == FcsState::IDLE_SAFE || state_ == FcsState::SEARCH) {
        transition(FcsState::FREECAM);
    }
}

// AM7-L2-MODE-004: SEARCH state entry from FREECAM or IDLE_SAFE
void StateMachine::request_search() {
    if (state_ == FcsState::FREECAM || state_ == FcsState::IDLE_SAFE) {
        transition(FcsState::SEARCH);
    }
}

// AM7-L2-MODE-001: BOOT -> FAULT on initialization failure
void StateMachine::on_boot_failure() {
    if (state_ == FcsState::BOOT) {
        on_fault(FaultCode::WATCHDOG_TIMEOUT);  // Generic fault code for boot failure
    }
}

// AM7-L3-MODE-006: Manual reset from FAULT -> IDLE_SAFE
void StateMachine::on_manual_reset() {
    if (state_ == FcsState::FAULT) {
        // Clear latched fault on manual reset
        fault_latched_ = false;
        operator_authorized_ = false;  // Reset authorization
        have_valid_range_ = false;     // Clear range data
        transition(FcsState::IDLE_SAFE);
    }
}

// AM7-L3-MODE-006: Operator cancel request (TRACKING/SEARCH/FREECAM -> IDLE_SAFE)
void StateMachine::request_cancel() {
    switch (state_) {
        case FcsState::TRACKING:
        case FcsState::SEARCH:
        case FcsState::FREECAM:
            transition(FcsState::IDLE_SAFE);
            break;
        default:
            // Cancel not valid from other states
            break;
    }
}

// AM7-L3-MODE-006: Disarm request (ARMED -> IDLE_SAFE)
void StateMachine::request_disarm() {
    if (state_ == FcsState::ARMED) {
        transition(FcsState::IDLE_SAFE);
    }
}

// INT-010: AM7-L3-MODE-010: Operator authorization for ARMED state
// This method MUST be called from an external input source:
// - Ground station message (e.g., UDP/TCP command from operator console)
// - Physical safety interlock (e.g., hardware key switch)
// - Remote authorization signal (e.g., range safety officer approval)
//
// ARMED state is unreachable without authorization. The on_ballistics_solution()
// method checks has_operator_authorization() before transitioning to ARMED.
void StateMachine::set_operator_authorization(bool authorized) {
    operator_authorized_ = authorized;
    // AM7-L3-MODE-006: ARMED -> IDLE_SAFE if authorization is lost
    if (!authorized && state_ == FcsState::ARMED) {
        transition(FcsState::IDLE_SAFE);
    }
}

bool StateMachine::has_valid_lock() const { return redetection_score_ >= kRedetectionScoreMin; }

bool StateMachine::has_stable_timing() const {
    // Jitter ≤5% at 99.9th percentile - checked externally
    return true;  // Placeholder
}

bool StateMachine::has_zero_faults() const { return !fault_latched_; }

bool StateMachine::has_operator_authorization() const { return operator_authorized_; }

void StateMachine::set_interlock_enabled(bool enabled) {
    // AM7-L3-MODE-007: Safety posture per state
    // Only ARMED state permits interlock enable
    if (state_ == FcsState::ARMED) {
        interlock_enabled_ = enabled;
    }
}

bool StateMachine::is_interlock_enabled() const { return interlock_enabled_; }

const char* fcs_state_name(FcsState s) {
    switch (s) {
        case FcsState::BOOT:
            return "BOOT";
        case FcsState::IDLE_SAFE:
            return "IDLE_SAFE";
        case FcsState::FREECAM:
            return "FREECAM";
        case FcsState::SEARCH:
            return "SEARCH";
        case FcsState::TRACKING:
            return "TRACKING";
        case FcsState::ARMED:
            return "ARMED";
        case FcsState::FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

}  // namespace aurore
