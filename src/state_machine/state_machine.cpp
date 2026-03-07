#include "aurore/state_machine.hpp"
#include <cmath>
#include <stdexcept>

namespace aurore {

StateMachine::StateMachine() = default;

FcsState StateMachine::state() const { return state_; }

void StateMachine::set_state_change_callback(StateChangeCb cb) {
    on_change_ = std::move(cb);
}

void StateMachine::force_state_for_test(FcsState s) {
    state_ = s;
    state_age_ = {};
}

void StateMachine::transition(FcsState next) {
    // AM7-L3-MODE-009: No state shall transition directly to ARMED except TRACKING
    if (next == FcsState::ARMED && state_ != FcsState::TRACKING) {
        return;  // Reject invalid transition
    }
    
    FcsState prev = state_;
    state_ = next;
    state_age_ = {};
    enter_state(next);
    if (on_change_) on_change_(prev, next);
}

void StateMachine::enter_state(FcsState s) {
    gimbal_ = {};
    align_sustained_ms_ = 0;
    
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
            first_detection_ = d;
            have_first_detection_ = true;
            transition(FcsState::TRACKING);
            break;
            
        case FcsState::FREECAM:
        case FcsState::IDLE_SAFE:
            // Store detection but don't auto-transition
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
        }
    }
}

void StateMachine::on_gimbal_status(const GimbalStatus& g) {
    gimbal_ = g;
    
    if (state_ == FcsState::SEARCH) {
        // AM7-L2-MODE-004: SEARCH - wait for gimbal to settle
        if (g.az_error_deg < kGimbalErrorMaxDeg &&
            g.velocity_deg_s < kGimbalVelocityMaxDs) {
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

void StateMachine::on_lrf_range(float /*range_m*/) {
    // Range data handling - validate per AM7-L3-SAFE-002
}

void StateMachine::on_ballistics_solution(const FireControlSolution& sol) {
    solution_ = sol;

    // AM7-L2-MODE-006: ARMED state entry from TRACKING
    // Requires: (a) valid lock, (b) stable timing, (c) zero faults, (d) operator authorization
    // INT-010: ARMED state is UNREACHABLE without operator authorization per AM7-L3-MODE-010.
    // The has_operator_authorization() check ensures external authorization signal is required.
    if (state_ == FcsState::TRACKING && sol.p_hit >= kPHitMin) {
        // Check all ARMED entry conditions (AM7-L3-MODE-010)
        if (has_valid_lock() && has_stable_timing() && has_zero_faults() && has_operator_authorization()) {
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
}

bool StateMachine::has_valid_lock() const {
    return redetection_score_ >= kRedetectionScoreMin;
}

bool StateMachine::has_stable_timing() const {
    // Jitter ≤5% at 99.9th percentile - checked externally
    return true;  // Placeholder
}

bool StateMachine::has_zero_faults() const {
    return !fault_latched_;
}

bool StateMachine::has_operator_authorization() const {
    return operator_authorized_;
}

void StateMachine::set_interlock_enabled(bool enabled) {
    // AM7-L3-MODE-007: Safety posture per state
    // Only ARMED state permits interlock enable
    if (state_ == FcsState::ARMED) {
        interlock_enabled_ = enabled;
    }
}

bool StateMachine::is_interlock_enabled() const {
    return interlock_enabled_;
}

const char* fcs_state_name(FcsState s) {
    switch (s) {
        case FcsState::BOOT:        return "BOOT";
        case FcsState::IDLE_SAFE:   return "IDLE_SAFE";
        case FcsState::FREECAM:     return "FREECAM";
        case FcsState::SEARCH:      return "SEARCH";
        case FcsState::TRACKING:    return "TRACKING";
        case FcsState::ARMED:       return "ARMED";
        case FcsState::FAULT:       return "FAULT";
        default:                    return "UNKNOWN";
    }
}

}  // namespace aurore
