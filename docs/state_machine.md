# State Machine

**Module:** `aurore::StateMachine`  
**Header:** `include/aurore/state_machine.hpp`  
**Implementation:** `src/state_machine/state_machine.cpp`

---

## Overview

The State Machine implements the Fire Control System (FCS) operational mode management per spec.md requirements AM7-L1-MODE-002 and AM7-L3-MODE-001. It manages transitions between seven operational states with guarded entry/exit conditions.

**Related Requirements:**
- AM7-L1-MODE-002: System shall implement 7-state FCS state machine
- AM7-L2-MODE-001 through AM7-L2-MODE-007: State definitions and behaviors
- AM7-L3-MODE-001: State transition table
- AM7-L3-MODE-002: State diagram
- AM7-L3-MODE-010: ARMED state requires operator authorization
- AM7-L3-MODE-011: Fault codes for fault transition

---

## State Diagram

```
                         ┌─────────────┐
                         │    BOOT     │
                         └──────┬──────┘
                                │ (init OK)
                                ▼
                         ┌─────────────┐
                    ┌────│  IDLE_SAFE  │◄───┐
                    │    └──────┬──────┘    │
               (request)       │(request)    │(cancel/fault)
                    │          │             │
                    ▼          ▼             │
               ┌─────────┐ ┌────────┐        │
               │ FREECAM │ │ SEARCH │────────┤
               └────┬────┘ └───┬────┘        │
                    │          │(lock valid)  │
                    │          ▼             │
                    │    ┌──────────┐        │
                    │    │ TRACKING │◄───────┤
                    │    └────┬─────┘        │
                    │         │(lock+stable) │
                    │         ▼              │
                    │    ┌────────┐          │
                    └────│ ARMED  │──────────┘
                         └───┬────┘
                             │
               (any fault)   │
           ┌─────────────────┘
           ▼
    ┌─────────────┐
    │    FAULT    │
    └──────┬──────┘
           │
    (power cycle / manual reset)
           │
           └──────────────────────┘
```

---

## State Definitions

### BOOT (0)

**Entry Conditions:** System power-on or reset

**Behavior:**
- Hardware initialization (camera, I2C, GPIO)
- Memory lock (`mlockall`)
- Self-test execution
- Thread creation with SCHED_FIFO priorities

**Exit Conditions:**
- → IDLE_SAFE: All initialization complete, self-test passed
- → FAULT: Initialization failure, self-test failure

**Related Requirements:** AM7-L2-MODE-001

---

### IDLE_SAFE (1)

**Entry Conditions:** From BOOT (init OK) or FAULT (reset) or any state (cancel/fault)

**Behavior:**
- Interlock INHIBIT (servo command disabled)
- Gimbal HOLD position
- No target lock attempts
- Safety monitor active at 1kHz

**Exit Conditions:**
- → FREECAM: Operator requests manual control
- → SEARCH: Operator requests auto-acquisition
- → FAULT: Any fault detected

**Related Requirements:** AM7-L2-MODE-002

---

### FREECAM (2)

**Entry Conditions:** From IDLE_SAFE (operator request)

**Behavior:**
- Manual gimbal control via operator input
- No automatic target acquisition
- Vision pipeline running (no lock)
- Interlock INHIBIT

**Exit Conditions:**
- → IDLE_SAFE: Operator cancels manual control
- → SEARCH: Operator requests auto-acquisition
- → FAULT: Any fault detected

**Related Requirements:** AM7-L2-MODE-003

---

### SEARCH (3)

**Entry Conditions:** From IDLE_SAFE or FREECAM (operator request)

**Behavior:**
- Automatic FOV scan pattern
- Target detection active (ORB detector)
- Gimbal sweeps predefined pattern
- 5-second timeout (AM7-L2-MODE-004)

**Exit Conditions:**
- → IDLE_SAFE: Timeout, operator cancel
- → TRACKING: Valid target lock acquired
- → FAULT: Any fault detected

**Related Requirements:** AM7-L2-MODE-004

---

### TRACKING (4)

**Entry Conditions:** From SEARCH (valid lock) or ARMED (lock lost)

**Behavior:**
- Continuous target lock via KCF tracker
- Gimbal actively tracks target centroid
- Ballistic solution computed each frame
- Interlock INHIBIT (not yet armed)

**Exit Conditions:**
- → IDLE_SAFE: Target lost, operator cancel
- → ARMED: Lock stable + operator authorization
- → SEARCH: Target lost, reacquire
- → FAULT: Any fault detected

**Related Requirements:** AM7-L2-MODE-005

---

### ARMED (5)

**Entry Conditions:** From TRACKING (lock stable + operator authorization)

**Behavior:**
- Continuous target lock maintained
- Interlock ENABLE permitted
- Effector trigger permitted
- 100ms timeout if authorization withdrawn (AM7-L3-MODE-010)

**Exit Conditions:**
- → IDLE_SAFE: Operator cancel, authorization withdrawn
- → TRACKING: Lock temporarily lost (recoverable)
- → FAULT: Any fault detected

**Related Requirements:** AM7-L2-MODE-006, AM7-L3-MODE-010

---

### FAULT (6)

**Entry Conditions:** From any state (fault detected)

**Behavior:**
- Interlock INHIBIT (forced)
- Gimbal HOLD position
- All outputs disabled
- Fault latched (non-clearable except power cycle)

**Exit Conditions:**
- → BOOT: Power cycle or manual reset (if implemented)
- → IDLE_SAFE: Manual reset after fault cleared

**Related Requirements:** AM7-L2-MODE-007, AM7-L3-MODE-011

---

## Transition Table

Per AM7-L3-MODE-001:

| Source     | BOOT | IDLE_SAFE | FREECAM | SEARCH | TRACKING | ARMED | FAULT |
|------------|------|-----------|---------|--------|----------|-------|-------|
| BOOT       | —    | ✓         | —       | —      | —        | —     | ✓     |
| IDLE_SAFE  | —    | —         | ✓       | ✓      | —        | —     | ✓     |
| FREECAM    | —    | ✓         | —       | ✓      | —        | —     | ✓     |
| SEARCH     | —    | ✓         | —       | —      | ✓        | —     | ✓     |
| TRACKING   | —    | ✓         | —       | ✓      | —        | ✓     | ✓     |
| ARMED      | —    | ✓         | —       | —      | ✓        | —     | ✓     |
| FAULT      | ✓    | ✓         | —       | —      | —        | —     | —     |

---

## Usage

### Basic Initialization

```cpp
#include "aurore/state_machine.hpp"

aurore::StateMachine fsm;

// Optional: Set state change callback for telemetry/logging
fsm.set_state_change_callback([](aurore::FcsState from, aurore::FcsState to) {
    log_info("State transition: %s → %s",
             fcs_state_name(from), fcs_state_name(to));
});
```

### Per-Frame Updates

```cpp
void control_cycle() {
    // Feed state machine with sensor/actuator data
    fsm.on_detection(detection);           // From vision pipeline
    fsm.on_tracker_initialized(track_sol); // From tracker init
    fsm.on_tracker_update(track_sol);      // From tracker update
    fsm.on_gimbal_status(gimbal_status);   // From Fusion HAT
    fsm.on_lrf_range(range_m);             // From laser rangefinder
    fsm.on_ballistics_solution(solution);  // From BallisticSolver
    
    // Advance state machine (call at 120Hz)
    fsm.tick(std::chrono::milliseconds(8));  // 8.333ms ≈ 120Hz
}
```

### Operator Mode Requests

```cpp
// Operator requests manual gimbal control
void on_freecam_button_pressed() {
    fsm.request_freecam();
}

// Operator requests auto target acquisition
void on_search_button_pressed() {
    fsm.request_search();
}

// Operator authorization for ARMED state (AM7-L3-MODE-010)
void on_arm_authorization_received() {
    fsm.set_operator_authorization(true);
}

// Safety interlock control (AM7-L3-MODE-007)
void on_interlock_toggle(bool enabled) {
    fsm.set_interlock_enabled(enabled);
}
```

### Fault Handling

```cpp
// Fault from any source triggers FAULT state
void on_camera_timeout() {
    fsm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
}

void on_gimbal_timeout() {
    fsm.on_fault(aurore::FaultCode::GIMBAL_TIMEOUT);
}

void on_range_data_stale() {
    fsm.on_fault(aurore::FaultCode::RANGE_DATA_STALE);
}

void on_watchdog_timeout() {
    fsm.on_fault(aurore::FaultCode::WATCHDOG_TIMEOUT);
}
```

### State Queries

```cpp
// Current state
aurore::FcsState current = fsm.state();

// State predicates
if (fsm.has_valid_lock()) {
    // Target locked and tracking
}

if (fsm.has_stable_timing()) {
    // Frame timing within tolerance
}

if (fsm.has_zero_faults()) {
    // No active faults
}

if (fsm.has_operator_authorization()) {
    // ARMED state permitted
}

if (fsm.is_interlock_enabled()) {
    // Effector trigger permitted
}
```

---

## Guard Conditions

### TRACKING Entry Guards

| Condition | Threshold | Source |
|-----------|-----------|--------|
| Detection confidence | ≥ 0.7 | `on_detection()` |
| Tracker PSR | > 0.035 | `on_tracker_update()` |
| Gimbal error | ≤ 2° | `on_gimbal_status()` |
| Gimbal velocity | ≤ 5°/s | `on_gimbal_status()` |
| Settled frames | ≥ 3 | `on_gimbal_status()` |

### ARMED Entry Guards

| Condition | Threshold | Source |
|-----------|-----------|--------|
| Valid lock | Yes | `has_valid_lock()` |
| Stable timing | Yes | `has_stable_timing()` |
| Zero faults | Yes | `has_zero_faults()` |
| Operator authorization | Yes | `set_operator_authorization(true)` |
| Alignment sustained | ≥ 20ms | Internal counter |

### FAULT Entry (from any state)

| Fault Code | Trigger |
|------------|---------|
| `CAMERA_TIMEOUT` | Frame not received within 20ms |
| `GIMBAL_TIMEOUT` | I2C response timeout > 10ms |
| `RANGE_DATA_STALE` | Range age > 100ms |
| `RANGE_DATA_INVALID` | NaN, infinity, or out of range |
| `AUTH_FAILURE` | Authentication failure |
| `SEQUENCE_GAP` | Command sequence gap > 1000 |
| `TEMPERATURE_CRITICAL` | CPU temp > 85°C |
| `WATCHDOG_TIMEOUT` | Watchdog not kicked within 60ms |

---

## Fault Codes (AM7-L3-MODE-011)

```cpp
enum class FaultCode : uint8_t {
    CAMERA_TIMEOUT = 0,      // Vision pipeline timeout
    GIMBAL_TIMEOUT = 1,      // I2C communication timeout
    RANGE_DATA_STALE = 2,    // Range data age > 100ms
    RANGE_DATA_INVALID = 3,  // NaN, infinity, out of range
    AUTH_FAILURE = 4,        // Authentication failure
    SEQUENCE_GAP = 5,        // Command sequence gap detected
    TEMPERATURE_CRITICAL = 6,// CPU temperature > 85°C
    WATCHDOG_TIMEOUT = 7,    // Watchdog not kicked within 60ms
};
```

---

## Integration with Telemetry

State transitions are automatically logged via TelemetryWriter:

```cpp
// In state change callback
fsm.set_state_change_callback([&telemetry](aurore::FcsState from, aurore::FcsState to) {
    telemetry.log_event(
        aurore::TelemetryEventId::STATE_TRANSITION,
        aurore::TelemetrySeverity::INFO,
        std::string("Transition: ") + fcs_state_name(from) + " → " + fcs_state_name(to)
    );
});
```

---

## Testing

### Unit Test Example

```cpp
#include "aurore/state_machine.hpp"
#include <gtest/gtest.h>

TEST(StateMachineTest, BootToIdleSafeTransition) {
    aurore::StateMachine fsm;
    
    // Initial state is BOOT
    EXPECT_EQ(fsm.state(), aurore::FcsState::BOOT);
    
    // Simulate successful init (tick without faults)
    fsm.tick(std::chrono::milliseconds(8));
    
    // Should transition to IDLE_SAFE
    EXPECT_EQ(fsm.state(), aurore::FcsState::IDLE_SAFE);
}

TEST(StateMachineTest, FaultFromAnyState) {
    aurore::StateMachine fsm;
    
    // Get to TRACKING state
    // ... (setup omitted)
    
    // Inject fault
    fsm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
    
    // Should be in FAULT state
    EXPECT_EQ(fsm.state(), aurore::FcsState::FAULT);
    EXPECT_FALSE(fsm.has_zero_faults());
}
```

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| `tick()` execution time | <10μs | Pure logic, no I/O |
| Memory footprint | ~200 bytes | State + guards + counters |
| Thread safety | Single-threaded | Call from control loop only |
| Callback overhead | ~1-2μs | If state change callback set |

---

## Troubleshooting

### Issue: State stuck in BOOT

**Symptoms:** Never transitions to IDLE_SAFE

**Check:**
1. All hardware initialization completed
2. No faults injected during BOOT
3. `tick()` being called at expected rate

### Issue: Cannot enter ARMED state

**Symptoms:** Stays in TRACKING despite good lock

**Check:**
1. `set_operator_authorization(true)` called
2. Authorization not expired (100ms timeout)
3. Alignment sustained for ≥20ms
4. All guards met (lock valid, timing stable, zero faults)

### Issue: Unexpected FAULT transitions

**Symptoms:** Random transitions to FAULT state

**Check:**
1. Fault source logs (telemetry events)
2. Camera frame timing (CAMERA_TIMEOUT)
3. I2C communication (GIMBAL_TIMEOUT)
4. Range data freshness (RANGE_DATA_STALE)

---

## Related Documentation

- [Telemetry Writer](telemetry.md) - Logging state transitions
- [spec.md](../spec.md) - Requirements AM7-L1-MODE-* and AM7-L3-MODE-*
- [Fusion HAT Driver](../include/aurore/fusion_hat.hpp) - Gimbal control interface
- [Tracker](../include/aurore/tracker.hpp) - KCF tracker interface
