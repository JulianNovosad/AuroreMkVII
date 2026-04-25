# Test Coverage & Missing Safety/Behavior Analysis

## Aurore MkVII - Real-Time Turret Control System

---

## 1. System Map

### Control Loops

| Loop | Thread | Priority | CPU | Period | WCET Budget |
|------|--------|----------|-----|--------|--------------|
| vision_pipeline | SafetyMonitor | 99 | 3 | 1ms | 5ms |
| vision_pipeline | VisionPipeline | 90 | 2 | 8.333ms | 5ms |
| track_compute | TrackCompute | 85 | 2 | 8.333ms | 2ms |
| actuation_output | ActuationOutput | 95 | 2 | 8.333ms | 2ms |

### State Machine (7-State FCS)

**BOOT → IDLE_SAFE → FREECAM / SEARCH → TRACKING → ARMED → FAULT**

- BOOT: Hardware init, memory lock, self-test
- IDLE_SAFE: Inhibit, gimbal hold, no lock
- FREECAM: Manual gimbal control
- SEARCH: Auto FOV scan, target acquisition
- TRACKING: Continuous target lock
- ARMED: Interlock enable permitted
- FAULT: Latched fault (power cycle required)

### Sensors

| Sensor | Interface | Protocol | Failure Modes |
|--------|-----------|----------|--------------|
| Camera (Sony IMX708) | libcamera/DMA | RAW10 @ 120Hz | Timeout, buffer overrun, stale frames |
| Laser Rangefinder (M01) | UART | 9600 8N1 | CRC errors, frameErrors, noResponse |
| IMU | UDP | 100Hz | Packet loss, stale data |
| Fusion HAT+ | I2C | 100kHz | NACK, timeout |

### Actuators

| Effector | Interface | Constraints |
|---------|-----------|-------------|
| Gimbal (2-DOF) | Fusion HAT+ I2C | Az: ±90°, El: -10° to +45°. Rate: ≤60°/s, Accel: ≤120°/s² |

### Safety Layers

- **1kHz SafetyMonitor**: Vision/actuation deadline monitoring, software watchdog (60ms timeout)
- **InterlockController**: GPIO-based hardware interlock with self-test
- **Software Watchdog**: 50ms kick interval, 60ms timeout

---

## 2. Coverage Audit of Existing Tests (43 tests total)

### Unit Tests (31 tests)

| Category | Tests | Coverage Assessment |
|----------|-------|---------------------|
| Ring Buffer | 1 | ✓ Basic SPSC push/pop, full/empty |
| Timing | 1 | ✓ ThreadTiming, DeadlineMonitor |
| Safety Monitor | 2 | ✓ Fault detection, callbacks, watchdog |
| State Machine | 3 | ✓ Transition table, range validation |
| Gimbal Controller | 1 | ✓ Rate/accel limiting |
| Ballistics | 2 | ✓ RK4, p_hit, lookup tables |
| Detector/Tracker | 1 | AprilTag, KCF |
| Interlock | 1 | ✓ GPIO interlock, self-test |
| Laser Rangefinder | 2 | ✓ M01/Modbus protocols |
| Config/Security | 3 | ConfigLoader, CRC-16, HMAC |

**Assessment**: Core logic and state machine are well-covered.

### Integration Tests (5 tests)

| Test | Coverage |
|------|----------|
| safety_fault_injection_test | ✓ FMEA coverage, fault detection/response |
| laser_validation_test | LRF + state machine integration |
| Timing/jitter analysis | ✓ No mocks - hardware required |

**Gap**: No end-to-end pipeline tests (vision → track → actuation).

### Hardware-in-Loop (3 tests)

| Test | Hardware Required |
|------|------------------|
| gimbal_actuation_test | Fusion HAT+, gimbal |
| boresight_convergence_test | LRF, camera, gimbal |
| integration_check | All sensors |

### Stress/Fault Tests (4 tests)

| Test | Coverage |
|------|----------|
| core_stress_test | High-frequency ring buffer |
| safety_stress_test | Concurrent deadline misses |
| state_machine_stress_test | Rapid state transitions |
| ballistics_stress_test | 1kHz RT bench |

---

## 3. Missing Behavioral Coverage

### Control System Failure Modes (CRITICAL)

| # | Missing Test | Risk | Trigger Condition |
|---|-------------|-----|-------------------|
| C1 | PID oscillation detection | Gimbal runaway | Gain too high, target jitter |
| C2 | Feedback delay sensitivity | Tracking instability | >16ms frame latency |
| C3 | Actuator saturation handling | Stuck gimbal at limit | Rapid large commands |
| C4 | Rate+accel limit interaction | Jerky/unpredictable motion | Velocity + accel limits interact |

### Sensor Failures (CRITICAL)

| # | Missing Test | Risk | Trigger Condition |
|---|-------------|-----|-------------------|
| S1 | LRF noisy readings filtering | Bad range → miss | Serial LRF noise |
| S2 | Camera dropped frame detection | Vision pipeline stall | DMA buffer exhaustion |
| S3 | Stale sensor rejection | Range fault ignored | >100ms old data |
| S4 | Corrupted/out-of-range data | Invalid FAULT trigger | Range outside [0.5, 5000]m |

### Communication Failures (IMPORTANT)

| # | Missing Test | Risk | Trigger Condition |
|---|-------------|-----|-------------------|
| N1 | AuroraLink disconnect handling | Lost control | TCP disconnect |
| N2 | Command queue overflow | Dropped commands | UDP queue full |
| N3 | Partial message handling | Corrupted commands | Truncated protobuf |

### Safety System Gaps (CRITICAL)

| # | Missing Test | Risk | Trigger Condition |
|---|-------------|-----|-------------------|
| E1 | Emergency stop from all states | No ESTOP from ARMED | Hardware fault |
| E2 | Watchdog timeout behavior | Missed safety fault | Single missed kick |
| E3 | 1kHz safety loop failure | Missed deadline | CPU overload |
| E4 | Interlock fault + camera fault | Multiple simultaneous | Hardware cascade |

### Edge Cases (IMPORTANT)

| # | Missing Test | Risk | Trigger Condition |
|---|-------------|-----|-------------------|
| R1 | Max velocity saturation | Gimbal stalls | Large pixel offset |
| R2 | Rapid target switching | Lock loss | Multiple targets |
| R3 | Conflicting commands | Undefined state | AUTO + FREECAM |
| R4 | Startup/shutdown transition | Mid-operation state | FAULT during TRACKING |

---

## 4. Proposed Tests

### Critical Safety Tests (Must Implement)

#### TC-1: Emergency Stop from All States
```
Test: emergency_stop_all_states
Input: State = {BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED}
Expected: FAULT state, interlock_inhibited = true
Risk: No ESTOP from ARMED could cause physical harm
```

#### TC-2: Watchdog Miss Behavior
```
Test: watchdog_single_miss
Input: No kick for 60ms (slightly under 2x interval)
Expected: trigger_fault(WATCHDOG_FEED_FAILED)
Risk: Missed safety fault could cause cascade failure
```

#### TC-3: Multiple Fault Priority
```
Test: fault_priority_cascade
Input: Camera fault + Gimbal fault simultaneously
Expected: Highest severity fault latched, EMERGENCY triggered
Risk: Race condition in fault handling
```

#### TC-4: Interlock + Range Fault
```
Test: interlock_range_fault
Input: Interlock OPEN + Range stale
Expected: FAULT, actuation inhibited
Risk: Physical actuation with bad range could cause miss
```

#### TC-5: Vision Stall Detection
```
Test: vision_stall_recovery
Input: No vision frames for 25ms
Expected: VISION_STALLED fault after 2 consecutive misses
Risk: Missed vision could cause target loss
```

### Important Control Tests

#### TI-1: Rate Limit + Position Limit Interaction
```
Test: rate_position_limit_interaction
Input: Large pixel offset driving into position limit
Expected: limit_violated flag set, position clamped
Risk: Gimbal hits mechanical stop
```

#### TI-2: Sequence Gap Detection (AM7-L3-SEC-004)
```
Test: sequence_gap_detection
Input: Command sequence gap > 1000
Expected: sequence_gap_detected = true
Risk: Command injection detection failure
```

#### TI-3: Gimbal Source Race
```
Test: auto_freecam_source_race
Input: Rapid source switching AUTO ↔ FREECAM
Expected: No position jump, smooth transition
Risk: Undefined state
```

### Sensor Tests

#### TS-1: LRF Noise Filtering
```
Test: lrf_noise_filtering
Input: LRF readings with ±10% noise
Expected: Filtered output, noise flag set
Risk: Bad range data causes miss
```

#### TS-2: Stale Range Rejection
```
Test: stale_range_rejection (> 100ms)
Input: RangeData with age > 100ms
Expected: RANGE_DATA_STALE fault
Risk: Using old range data
```

### Integration Tests

#### TINT-1: Pipeline Stall Response
```
Test: pipeline_stall_response
Input: Vision pipeline stalls for 25ms+
Expected: All stages stall, FAULT triggered
Risk: No stall detection in end-to-end pipeline
```

#### TINT-2: State Transition During Fault
```
Test: state_transition_during_fault
Input: Recovery during TRACKING fault
Expected: IDLE_SAFE reached cleanly
Risk: Stuck in FAULT state
```

---

## 5. Prioritized Test Plan

### Priority Matrix

| Priority | Category | Tests | Coverage Impact |
|----------|---------|-------|-----------------|
| **CRITICAL** | Safety Fault Tests | 5 tests → Covers all ESTOP paths |
| **CRITICAL** | Watchdog Behavior | 2 tests → Covers safety monitor |
| **HIGH** | Control Limit Interaction | 4 tests → Physical safety |
| **MEDIUM** | Sensor Validation | 3 tests → Data integrity |
| **MEDIUM** | Integration | 2 tests → End-to-end |
| **LOW** | Edge Cases | 2 tests → Robustness |

### Test Count by Category

| Category | Existing | Proposed | Total |
|----------|----------|----------|-------|
| Unit Tests | 31 | 12 | 43 |
| Integration Tests | 5 | 4 | 9 |
| Hardware Tests | 3 | 0 | 3 |
| Safety/Fault Tests | 4 | 6 | 10 |
| **TOTAL** | **43** | **22** | **65** |

### Coverage Estimate

| System Risk | Before | After |
|-------------|--------|-------|
| Emergency Stop Coverage | 70% | 100% |
| Watchdog Behavior | 60% | 100% |
| Sensor Failure Modes | 50% | 80% |
| Control Limits | 75% | 100% |
| Integration Paths | 20% | 60% |

---

## 6. Test Implementation Strategy

### Phase 1: Critical Safety (3 tests)
Add to `tests/unit/safety_monitor_fault_test.cpp`:
1. `test_emergency_stop_all_states`
2. `test_watchdog_single_miss`
3. `test_fault_priority_cascade`

### Phase 2: Control Limits (4 tests)
Add to `tests/unit/gimbal_controller_test.cpp`:
1. `test_rate_position_limit_interaction`
2. `test_sequence_gap_detection`
3. `test_auto_freecam_source_race`
4. `test_position_saturation_recovery`

### Phase 3: Integration (4 tests)
New file `tests/integration/pipeline_fault_test.cpp`:
1. `test_pipeline_stall_response`
2. `test_interlock_range_fault`
3. `test_vision_stall_recovery`
4. `test_state_transition_during_fault`

### Phase 4: Sensor (3 tests)
Add to `tests/unit/laser_rangefinder_test.cpp`:
1. `test_lrf_noise_filtering`
2. `test_stale_range_rejection`
3. `test_range_boundary_corner_cases`

---

## Risk Mapping Summary

| Test | Prevents | Requirement |
|------|---------|-------------|
| emergency_stop_all_states | ESTOP failure | AM7-L3-MODE-006 |
| watchdog_single_miss | Missed safety | AM7-L3-SAFE-005 |
| fault_priority_cascade | Race condition | AM7-L3-SAFE-001 |
| interlock_range_fault | Bad data actuation | AM7-L3-SAFE-002 |
| vision_stall_recovery | Blind operation | AM7-L3-VIS-004 |
| rate_position_limit_interaction | Mechanical damage | AM7-L3-ACT-003 |
| sequence_gap_detection | Command injection | AM7-L3-SEC-004 |
| pipeline_stall_response | Silent failure | AM7-L2-TIM-001 |

---

## Recommendation

**Do not implement arbitrary test counts.** Focus on:

1. **Critical Safety First**: 6 tests covering all emergency stop paths and watchdog behavior
2. **Control Limits Second**: 4 tests covering rate/position limit interaction
3. **Integration Third**: 4 tests covering end-to-end pipeline behavior
4. **Sensor Fourth**: 3 tests covering data validation

Total: **17 targeted tests** covering maximum risk with minimal overhead.

---

*Generated: 2026-04-25*
*System: Aurore MkVII - Real-Time Vision-Based Turret Control*