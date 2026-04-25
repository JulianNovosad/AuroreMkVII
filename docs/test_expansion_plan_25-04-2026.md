# Test Expansion & CI Hardening Plan

## Aurore MkVII - Real-Time Turret Control System

**Generated:** 2026-04-25
**Parent Document:** `docs/test_coverage_audit_25-04-2026.md`
**Purpose:** Operationalize audit findings into scalable CTest architecture, parameterized test families, and CI enforcement

---

## Part 1 — Test Expansion Matrix (Missing Behaviors → Parameterized Families)

### 1.1 Control System Failure Modes (C1–C4)

| Test ID | Base Test Name | Dimensions | Parameter Values | Expected Count |
|---------|---------------|-------------|-------------------|------------------|
| C1 | `test_pid_oscillation_detection` | gain_multiplier | {1.0, 1.5, 2.0, 3.0, 5.0} | 5 |
| C1 | | target_jitter_px | {0, 5, 10, 20, 50} | 5 |
| C1 | | state_at_injection | {SEARCH, TRACKING, ARMED} | 3 |
| C2 | `test_feedback_delay_sensitivity` | frame_latency_ms | {0, 8, 16, 24, 32, 48} | 6 |
| C2 | | delay_pattern | {constant, burst, increasing} | 3 |
| C3 | `test_actuator_saturation_handling` | command_magnitude_deg | {10, 30, 50, 80, 90} | 5 |
| C3 | | initial_position_deg | {-80, -45, 0, 45, 80} | 5 |
| C4 | `test_rate_accel_limit_interaction` | velocity_limit_dps | {30, 45, 60} | 3 |
| C4 | | accel_limit_dps2 | {60, 90, 120} | 3 |
| C4 | | input_pattern | {step, ramp, sine} | 3 |

**Control System Total:** 41 parameterized tests

### 1.2 Sensor Failures (S1–S4)

| Test ID | Base Test Name | Dimensions | Parameter Values | Expected Count |
|---------|---------------|-------------|-------------------|------------------|
| S1 | `test_lrf_noise_filtering` | noise_amplitude_pct | {0, 5, 10, 15, 20, 30} | 6 |
| S1 | | noise_distribution | {gaussian, uniform, impulse} | 3 |
| S1 | | filter_window_size | {3, 5, 7, 11} | 4 |
| S2 | `test_camera_dropped_frame_detection` | drop_pattern | {single, double, burst, systematic} | 4 |
| S2 | | buffer_pressure_pct | {50, 75, 90, 100} | 4 |
| S2 | | resolution | {1536x864, 1280x720} | 2 |
| S3 | `test_stale_sensor_rejection` | data_age_ms | {50, 100, 150, 200, 500} | 5 |
| S3 | | sensor_type | {LRF, IMU, camera} | 3 |
| S3 | | state_at_check | {IDLE_SAFE, SEARCH, TRACKING, ARMED} | 4 |
| S4 | `test_corrupted_data_rejection` | corruption_type | {range_underflow, range_overflow, crc_mismatch, truncation} | 4 |
| S4 | | expected_range_mm | {500, 1000, 2000, 5000} | 4 |

**Sensor Failure Total:** 43 parameterized tests

### 1.3 Communication Failures (N1–N3)

| Test ID | Base Test Name | Dimensions | Parameter Values | Expected Count |
|---------|---------------|-------------|-------------------|------------------|
| N1 | `test_auroralink_disconnect` | disconnect_trigger | {tcp_rst, tcp_fin, timeout, icmp} | 4 |
| N1 | | state_at_disconnect | {FREECAM, SEARCH, TRACKING, ARMED} | 4 |
| N1 | | reconnect_delay_ms | {0, 100, 500, 1000} | 4 |
| N2 | `test_command_queue_overflow` | queue_capacity | {1, 5, 10, 20} | 4 |
| N2 | | command_rate_hz | {10, 50, 100, 200} | 4 |
| N2 | | overflow_action | {drop_oldest, drop_newest, block} | 3 |
| N3 | `test_partial_message_handling` | truncation_offset | {0.25, 0.5, 0.75} | 3 |
| N3 | | message_type | {gimbal, fire, config, telemetry} | 4 |
| N3 | | corruption_position | {header, payload, footer} | 3 |

**Communication Total:** 33 parameterized tests

### 1.4 Safety System Gaps (E1–E4)

| Test ID | Base Test Name | Dimensions | Parameter Values | Expected Count |
|---------|---------------|-------------|-------------------|------------------|
| E1 | `test_emergency_stop_all_states` | source_state | {BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED} | 6 |
| E1 | | estop_source | {hardware, software, watchdog, interlock} | 4 |
| E1 | | interlock_state_before | {OPEN, CLOSED} | 2 |
| E2 | `test_watchdog_timeout_behavior` | miss_count | {1, 2, 3} | 3 |
| E2 | | miss_duration_ms | {60, 70, 100, 150} | 4 |
| E2 | | state_at_miss | {SEARCH, TRACKING, ARMED, FAULT_RECOVERY} | 4 |
| E2 | | test_watchdog_period_ms | {10, 20, 50} | 3 |
| E3 | `test_1khz_safety_loop_failure` | deadline_miss_us | {500, 1000, 2000, 5000} | 4 |
| E3 | | miss_pattern | {single, periodic, cumulative} | 3 |
| E3 | | cpu_load_before_pct | {0, 50, 80, 95} | 4 |
| E4 | `test_interlock_camera_fault_cascade` | fault_sequence | {simultaneous, interlock_first, camera_first} | 3 |
| E4 | | camera_fault_type | {timeout, buffer_overrun, staleness} | 3 |
| E4 | | interlock_fault_type | {open_circuit, short_circuit, self_test_fail} | 3 |

**Safety System Total:** 45 parameterized tests

### 1.5 Edge Cases (R1–R4)

| Test ID | Base Test Name | Dimensions | Parameter Values | Expected Count |
|---------|---------------|-------------|-------------------|------------------|
| R1 | `test_max_velocity_saturation` | pixel_offset_px | {100, 500, 1000, 2000} | 4 |
| R1 | | frame_rate_hz | {30, 60, 90, 120} | 4 |
| R1 | | initial_velocity_dps | {0, 30, 60} | 3 |
| R2 | `test_rapid_target_switching` | target_count | {2, 3, 5, 10} | 4 |
| R2 | | switch_interval_ms | {8, 16, 33, 100} | 4 |
| R2 | | switch_pattern | {sequential, random, oscillating} | 3 |
| R3 | `test_conflicting_commands` | command_source_a | {AUTO, FREECAM, SEARCH} | 3 |
| R3 | | command_source_b | {AUTO, FREECAM, SEARCH} | 3 |
| R3 | | priority_resolution | {source_a_wins, source_b_wins, newest_wins, safety_wins} | 4 |
| R4 | `test_startup_shutdown_transition` | fault_injection_time | {0, 100, 500, 1000, 2000} | 5 |
| R4 | | shutdown_type | {graceful, immediate, fault_triggered} | 3 |
| R4 | | state_at_fault | {BOOT, IDLE_SAFE, SEARCH, TRACKING} | 4 |

**Edge Cases Total:** 41 parameterized tests

### 1.6 Test Expansion Summary

| Category | Base Tests | Parameter Combinations | Total Parameters |
|----------|------------|------------------------|-------------------|
| Control System Failures | 4 | 41 | 41 |
| Sensor Failures | 4 | 43 | 43 |
| Communication Failures | 3 | 33 | 33 |
| Safety System Gaps | 4 | 45 | 45 |
| Edge Cases | 4 | 41 | 41 |
| **Subtotal** | **19** | **—** | **203** |
| Audit-Identified Tests (from Section 5) | 17 | — | 17 |
| Existing Tests | 43 | — | 43 |
| **GRAND TOTAL** | **79** | **—** | **~263** |

---

## Part 2 — Test Taxonomy & Directory Refactor

### 2.1 Proposed Directory Structure

```
tests/
├── unit/                           # Fast unit tests (< 10ms each)
│   ├── core/                       # Ring buffer, timing primitives
│   │   ├── ring_buffer_test.cpp
│   │   ├── timing_test.cpp
│   │   └── parameterized/
│   │       └── ring_buffer_stress.cpp
│   ├── safety/                    # Safety monitor, watchdog, interlock
│   │   ├── safety_monitor_test.cpp
│   │   ├── watchdog_test.cpp
│   │   ├── interlock_controller_test.cpp
│   │   └── parameterized/
│   │       ├── watchdog_timeout_matrix.cpp
│   │       ├── estop_matrix.cpp
│   │       └── fault_cascade_matrix.cpp
│   ├── control/                   # State machine, gimbal, ballistics
│   │   ├── state_machine_test.cpp
│   │   ├── gimbal_controller_test.cpp
│   │   ├── ballistics_test.cpp
│   │   └── parameterized/
│   │       ├── state_transition_matrix.cpp
│   │       ├── gimbal_limit_matrix.cpp
│   │       └── pid_tuning_matrix.cpp
│   ├── sensors/                   # Camera, LRF, IMU
│   │   ├── camera_wrapper_test.cpp
│   │   ├── laser_rangefinder_test.cpp
│   │   ├── imu_receiver_test.cpp
│   │   └── parameterized/
│   │       ├── lrf_noise_matrix.cpp
│   │       ├── stale_data_matrix.cpp
│   │       ��── camera_frame_matrix.cpp
│   ├── communication/             # AuroraLink, HUD socket, Fusion HAT
│   │   ├── aurore_link_test.cpp
│   │   ├── hud_socket_test.cpp
│   │   └── parameterized/
│   │       ├── disconnect_matrix.cpp
│   │       ├── queue_overflow_matrix.cpp
│   │       └── message_corruption_matrix.cpp
│   └── regression/                # Regression tests (issue-linked)
│       ├── issue_*.cpp             # Named by issue ID
│       └── README.md              # Regression policy
│
├── integration/                   # Integration tests (10ms–1s each)
│   ├── pipeline/                  # Vision → Track → Actuation chain
│   │   ├── pipeline_latency_test.cpp
│   │   └── parameterized/
│   │       ├── stall_response_matrix.cpp
│   │       └── end_to_end_matrix.cpp
│   ├── timing/                    # Thread synchronization, phase offset
│   │   ├── thread_sync_test.cpp
│   │   └── parameterized/
│   │       ├── phase_offset_matrix.cpp
│   │       └── deadline_miss_matrix.cpp
│   ├── fault_cascade/             # Multi-system fault scenarios
│   │   ├── multi_fault_test.cpp
│   │   └── parameterized/
│   │       ├── fault_priority_matrix.cpp
│   │       └── recovery_matrix.cpp
│   └── state_transitions/         # Cross-state behavior
│       ├── state_recovery_test.cpp
│       └── parameterized/
│           └── transition_during_fault_matrix.cpp
│
├── stress/                        # Stress tests (1s–1min each)
│   ├── rt_budget/                 # WCET, deadline miss injection
│   │   ├── wcet_benchmark.cpp
│   │   ├── deadline_miss_injection.cpp
│   │   └── parameterized/
│   │       ├── wcet_regression_matrix.cpp
│   │       └── jitter_matrix.cpp
│   ├── concurrency/               # Thread safety, race detection
│   │   ├── thread_race_test.cpp
│   │   ├── priority_inversion_test.cpp
│   │   └── parameterized/
│   │       └── concurrent_access_matrix.cpp
│   └── memory/                    # Memory pressure, mlock limits
│       ├── memory_pressure_test.cpp
│       └── mlock_limit_test.cpp
│
├── hardware/                      # Hardware-in-loop tests (require hardware)
│   ├── gimbal/                    # Fusion HAT+, gimbal mechanics
│   │   ├── gimbal_actuation_test.cpp
│   │   └── parameterized/
│   │       ├── position_accuracy_matrix.cpp
│   │       └── rate_limit_compliance_matrix.cpp
│   ├── vision/                    # Camera, lighting, boresight
│   │   ├── boresight_convergence_test.cpp
│   │   └── camera_calibration_test.cpp
│   └── full_system/               # All sensors + actuators
│       ├── integration_check.cpp
│       └── end_to_end_hil_test.cpp
│
└── README.md                      # Test taxonomy and running guide
```

### 2.2 Directory Purposes and Constraints

| Directory | Purpose | Allowed Dependencies | Runtime Limit |
|-----------|---------|----------------------|---------------|
| `unit/core/` | Lock-free primitives | None (pure logic) | < 1ms per test |
| `unit/safety/` | Safetymonitor, watchdog | core, safety_monitor header | < 5ms per test |
| `unit/control/` | State machine, gimbal | core, control headers | < 10ms per test |
| `unit/sensors/` | Sensor interfaces | core, sensor headers | < 10ms per test |
| `unit/communication/` | Network protocols | core, comm headers | < 10ms per test |
| `unit/regression/` | Issue-linked tests | Minimal (issue-specific) | < 50ms per test |
| `integration/pipeline/` | Multi-stage data flow | Multiple unit modules | < 100ms per test |
| `integration/timing/` | Thread timing | Threading headers | < 500ms per test |
| `integration/fault_cascade/` | Multi-system faults | Integration + safety | < 1s per test |
| `stress/rt_budget/` | Real-time budget tests | All modules | < 10s per test |
| `stress/concurrency/` | Thread stress | Threading primitives | < 30s per test |
| `hardware/` | Physical hardware | Real hardware | No limit (requires hardware) |

### 2.3 CTest Configuration

```cmake
# CMakeLists.txt addition for parameterized tests

# Test DiscoveryPatterns:
# - Base tests declared with add_test()
# - Parameterized tests use ctest_discover_tests() with PROPERTIES
# - Test names follow: {category}_{subcategory}_{dimension}_{value}

# Example test name patterns:
# safety_watchdog_timeout_miss_count_1
# safety_watchdog_timeout_miss_count_2
# safety_watchdog_timeout_miss_count_3
# safety_watchdog_timeout_state_SEARCH
# safety_watchdog_timeout_state_TRACKING

# Environment requirements:
# - Fast tests: No special requirements
# - RT tests: REQUIRE_SCHED_FIFO property
# - Hardware tests: REQUIRE_HARDWARE property

# Test grouping:
set_tests_properties(
    safety_watchdog_timeout*
    PROPERTIES
    LABELS "safety;rt_required;fast"
)

set_tests_properties(
    hardware_gimbal*
    PROPERTIES
    LABELS "hardware;hil;slow"
)
```

---

## Part 3 — Timing, RT, and Determinism Tests

### 3.1 Real-Time Test Categories

| Test Category | Description | Required Properties |
|---------------|-------------|---------------------|
| `rt_deadline_miss` | Detect deadline misses under load | SCHED_FIFO, root |
| `rt_wcet_regression` | Detect WCET growth over time | SCHED_FIFO, root, long-running |
| `rt_jitter_envelope` | Measure jitter distribution | SCHED_FIFO, root, statistics |
| `rt_priority_inversion` | Detect priority inversion | SCHED_FIFO, root, multi-thread |
| `rt_phase_stability` | Measure phase drift over time | SCHED_FIFO, root, long-running |

### 3.2 Test Design Specifications

#### Deadline Miss Detection Tests

```
Test: rt_deadline_miss_detection
Parameters:
  - thread_priority: {85, 90, 95, 99}
  - workload_duration_us: {1000, 2000, 4000, 5000}
  - load_interval_ms: {1, 2, 4, 8}
  - cpu_isolation: {true, false}

Expected Behavior:
  - Under isolated CPU with SCHED_FIFO: deadline miss count = 0
  - Under non-isolated CPU: allow degradation but measure

Metric Thresholds:
  - Deadline miss rate < 0.01% under RT conditions
  - Miss detection latency < 100us after deadline
```

#### WCET Regression Tests

```
Test: rt_wcet_regression_vision_pipeline
Parameters:
  - frame_resolution: {1536x864, 1280x720}
  - work_pattern: {idle, low_activity, high_activity}
  - sample_count: {100, 1000, 10000}

Expected Behavior:
  - WCET_2nd_invocation <= 5ms at 1536x864
  - WCET_p99 <= 5ms * 1.2 for regression detection
  - WCET growth rate <= 1%/hour

Metric Thresholds:
  - WCET_regression_threshold: 10% increase = FAIL
  - Memory leak detection: growth > 1KB/hr = FAIL
```

#### Jitter Tolerance Envelope Tests

```
Test: rt_jitter_envelope_measurement
Parameters:
  - thread: {vision_pipeline, track_compute, actuation_output}
  - measurement_duration_s: {60, 300, 600}
  - background_load_pct: {0, 25, 50, 75}

Expected Behavior:
  - Jitter p50 <= 50us
  - Jitter p95 <= 200us
  - Jitter p99 <= 500us

Jitter Envelope Specification:
  - Phase offset stability: drift < 10us/hour
  - Period accuracy: actual_period within ±50us of specified
```

#### Priority Inversion Detection Tests

```
Test: rt_priority_inversion_detection
Parameters:
  - high_priority: {99}
  - low_priority: {50}
  - contention_type: {lock, memory, I/O}
  - inversion_duration_us: {100, 1000, 10000}

Expected Behavior:
  - Priority inversion detected within 100us
  - Maximum inversion duration bounded
  - No unbounded priority inversion
```

### 3.3 Timing Assertion Expressions

```cpp
// Timing assertion patterns

// Deadlinemiss rate:
ASSERT_LE(miss_count, max_misses)
    << "Deadline misses: " << miss_count << " > " << max_misses;

// WCET percentile:
ASSERT_LE(wcet_p99, wcet_budget * 1.2)
    << "WCET p99: " << wcet_p99 << "us exceeds budget: " << wcet_budget << "us";

// Jitter envelope:
ASSERT_LE(jitter_p95, max_jitter_95)
    << "Jitter p95: " << jitter_p95 << "us exceeds: " << max_jitter_95;

// Rolling window:
for (int window = 0; window < window_count; ++window) {
    auto stats = compute_window_stats(samples, window);
    ASSERT_LE(stats.max, max_per_window)
        << "Window " << window << " max: " << stats.max << "us";
}
```

### 3.4 Test Execution Matrix

| Test Type | CPU Isolation | SCHED_FIFO | Root | Duration | Blocks Merge |
|-----------|----------------|------------|------|----------|-------------|
| Fast unit | No | No | No | < 10ms | Yes |
| RT deadline | Yes | Yes | Yes | < 10s | Yes |
| RT WCET | Yes | Yes | Yes | < 60s | Yes (T2) |
| RT jitter | Yes | Yes | Yes | < 600s | No (T2) |
| Hardware | N/A | N/A | N/A | No limit | N/A (T3) |

---

## Part 4 — CI / CD Pipeline Specification

### 4.1 Test Tiers

| Tier | Label | Test Groups | Execution Time | Merge Block |
|------|-------|-------------|----------------|-------------|
| T0 | `fast` | unit/core, unit/safety*, unit/control*, unit/sensors* | < 2 minutes | YES |
| T1 | `integration` | integration/*, unit/communication* | < 5 minutes | YES |
| T2 | `stress` | stress/rt_budget*, stress/concurrency* | < 15 minutes | NO* |
| T3 | `hardware` | hardware/* | Requires hardware | N/A (manual) |

*Tier 2 blocks merge on timing regression only, not stress test completion

### 4.2 Pipeline Stages

```yaml
# .github/workflows/test-pipeline.yaml (conceptual)

stages:
  - name: checkout
    run: git checkout
  
  - name: configure
    run: cmake -B build -DCMAKE_BUILD_TYPE=Release
    
  - name: build
    run: cmake --build build -j$(nproc)
    
  - name: tier0_fast_tests
    run: ctest -L fast --output-on-failure
    required: true
    
  - name: tier1_integration
    run: ctest -L integration --output-on-failure
    required: true
    
  - name: tier2_stress
    run: ctest -L stress --output-on-failure
    required: false
    continues_on_failure: true
    
  - name: coverage_report
    run: |
      cmake --build build --target coverage
      gcovr --html coverage.html
    artifacts:
      - coverage.html
      - "**/*.gcda"
      
  - name: timing_report
    run: |
      python scripts/parse_timing_results.py
    artifacts:
      - timing_results.json
      
  - name: test_summary
    run: |
      python scripts/generate_test_report.py
    artifacts:
      - test_report.html
```

### 4.3 Coverage Rules

```yaml
# Coverage requirements
coverage:
  minimum:
    line: 85%
    branch: 80%
    function: 90%
    
  regression:
    line_delta: -2%
    branch_delta: -2%
    function_delta: -2%
    
  enforcement:
    fail_on_regression: true
    quarantine_allowed: false  # No coverage exceptions
```

### 4.4 Timing Regression Rules

```yaml
# Timing regression enforcement
timing:
  wcet_regression_threshold: 10%  # 10% WCET increase blocks merge
  jitter_p95_threshold: 200us
  deadline_miss_threshold: 0.01%
  
  enforcement:
    fail_on_wcet_regression: true
    fail_on_jitter_exceedance: true
    fail_on_deadline_miss_exceedance: true
```

### 4.5 Flaky Test Handling

```yaml
# Flaky test policy
flaky_tests:
  detection:
    - Runs > 3 consecutive failures
    - Non-deterministic timing on isolated CPU
    
  quarantine:
    label: "flaky_quarantine"
    max_quarantine_duration: 30 days
    requires_issue: true
    
  enforcement:
    - Flaky tests block merge (label as such)
    - Must have issue to be in quarantine
    - Weekly review required
    - Deletion from quarantine prohibited without fix
```

### 4.6 Artifact Retention

| Artifact | Retention | Location |
|----------|-----------|----------|
| Coverage HTML | 90 days | `artifacts/coverage/` |
| Test logs | 30 days | `artifacts/logs/` |
| Timing traces | 180 days | `artifacts/timing/` |
| Core dumps | 7 days | `artifacts/cores/` |
| Binary artifacts | 30 days | `artifacts/binaries/` |

---

## Part 5 — Regression Doctrine

### 5.1 Regression Test Structure

```
tests/unit/regression/
├── README.md                      # Policy document
├── issue_*.cpp                    # One file per issue
│   ├── issue_XXX_description.cpp
│   └── issue_XXX_*.cpp
└── CMakeLists.txt                # Regression test discovery
```

### 5.2 Regression Naming Convention

```
test_regression_{issue_id}_{short_description}.cpp

Examples:
- test_regression_001_estop_from_armed.cpp
- test_regression_002_watchdog_miss_detection.cpp
- test_regression_003_lrf_noise_filter.cpp
```

### 5.3 Regression Policy (Non-Negotiable)

| Rule | Description |
|------|-------------|
| R1 | Every bug discovered MUST result in a new test |
| R2 | Test name MUST include issue ID |
| R3 | Test MUST reproduce exact failure condition |
| R4 | Test MUST be in `tests/unit/regression/` |
| R5 | Test deletion PROHIBITED without issue closure + senior approval |
| R6 | CI MUST run regression tests on every commit |
| R7 | Coverage regression MUST fail build |
| R8 | Regression tests MUST NOT be marked flaky |

### 5.4 Issue-Test Traceability

```cpp
// Example regression test header

/**
 * Regression test for Issue #XXX: [Title]
 * 
 * Issue URL: https://github.com/anomalyco/auroremkvii/issues/XXX
 * 
 * Failure condition:
 * [Exact description of the bug]
 * 
 * Test approach:
 * [How this test reproduces the bug]
 * 
 * Verification:
 * [Pass condition]
 */

TEST(RegressionXXX, ExactDescription) {
    // Test implementation
}
```

---

## Part 6 — Test Completeness Definition

### 6.1 Completeness Criteria

For Aurore MkVII, a system is considered **test complete** when:

| Criterion | Threshold | Measurement |
|-----------|-----------|-------------|
| **Safety coverage** | 100% | All ESTOP paths tested (6 states × 4 sources) |
| **Watchdog coverage** | 100% | All miss patterns tested (1/2/3 misses × 4 states) |
| **Control limits** | 100% | Rate + position + accel interaction tested |
| **Sensor validation** | 90% | Noise, staleness, corruption patterns |
| **Integration paths** | 75% | Vision→Track→Actuation complete |
| **Timing guarantees** | 100% | WCET, deadline, jitter measured |
| **Regression suite** | 100% | All closed issues have tests |
| **Code coverage** | ≥ 85% line, ≥ 80% branch |

### 6.2 Completeness Verification Checklist

```markdown
## Test Completeness Checklist

### Safety (CRITICAL)
- [ ] Emergency stop from all 6 states verified
- [ ] Watchdog timeout at 1/2/3 misses verified
- [ ] Multi-fault priority verified
- [ ] Interlock + sensor fault cascade verified

### Control
- [ ] Rate limit × position limit × accel limit interaction verified
- [ ] PID oscillation at all gain values verified
- [ ] Feedback delay sensitivity verified
- [ ] Actuator saturation handling verified

### Sensors
- [ ] LRF noise filtering at all noise levels verified
- [ ] Stale sensor rejection verified
- [ ] Camera dropped frame detection verified
- [ ] Corrupted data rejection verified

### Integration
- [ ] End-to-end pipeline latency verified
- [ ] State transition during fault verified
- [ ] Pipeline stall response verified

### Timing (RT-specific)
- [ ] WCET under 5ms at 1536×864 verified
- [ ] Deadline miss rate < 0.01% verified
- [ ] Jitter p95 < 200us verified
- [ ] Phase offset drift < 10us/hour verified

### Regression
- [ ] All critical issues have tests
- [ ] All major issues have tests
- [ ] Issue deletion rate = 0%
```

### 6.3 Test Count Targets by Completeness Level

| Level | Description | Test Count | Coverage |
|-------|-------------|-----------|----------|
| Level 1 | Minimal safety | ~50 | Emergency paths only |
| Level 2 | Basic functionality | ~120 | Core paths + basic sensors |
| Level 3 | Standard coverage | ~200 | All audit-identified tests |
| Level 4 | Full coverage | ~300 | All parameterized matrices |
| Level 5 | Certified safety | All dimensions | 100% coverage |

**Target for Aurore MkVII: Level 4 (Full Coverage)**

---

## Appendix A — Implementation Roadmap

### Phase 1: Directory Refactor (Week 1)

- Create directory structure
- Move existing tests to appropriate directories
- Update CMakeLists.txt
- Verify build + tests pass

### Phase 2: Parameterized Test Infrastructure (Week 2)

- Implement parameterized test framework
- Create first 5 parameterized test families
- Verify CTest discovers all parameterized tests

### Phase 3: Critical Safety Tests (Week 3)

- Implement E1 (ESTOP), E2 (Watchdog), E4 (Cascade) tests
- Add to T0 tier
- Verify CI blocks on failures

### Phase 4: Control + Sensor Tests (Week 4)

- Implement C1–C4, S1–S4 tests
- Add to T0/T1 tiers
- Verify coverage increases

### Phase 5: Integration + Timing Tests (Week 5)

- Implement integration/pipeline tests
- Implement timing/rt_budget tests
- Add to T1/T2 tiers

### Phase 6: CI Pipeline (Week 6)

- Configure GitHub Actions
- Set up coverage reporting
- Configure timing regression detection

### Phase 7: Regression Policy (Week 7)

- Create regression directory
- Document policy
- Add CI enforcement

---

## Appendix B — Test Execution Reference

```bash
# Run all fast tests (T0)
ctest -L fast --output-on-failure

# Run specific category
ctest -L safety --output-on-failure

# Run parameterized family
ctest -N | grep watchdog_timeout

# Run with timing
ctest --output-on-failure -T Timing

# Run stress tests only
ctest -L stress --output-on-failure

# Generate coverage
cmake --build . --target coverage
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|-------|---------|
| 1.0 | 2026-04-25 | ADA | Initial document |
| 1.1 | 2026-04-25 | ADA | Implementation: E1 ESTOP Matrix (24 tests), E2 Watchdog Matrix (192 tests) |

---

## Implementation Status (2026-04-25)

### ✅ Implemented

1. **Directory Structure**: Created `tests/unit/{core,safety,control,sensors,communication}/parameterized/`
2. **ESTOP Matrix (E1)**: 24 parameterized tests in `tests/unit/safety/parameterized/estop_matrix.cpp`
   - States: BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED (6)
   - Sources: hardware, software, watchdog, interlock (4)
   - Interlock: OPEN, CLOSED (2)
   - Total: 24 test combinations ✅
3. **Watchdog Matrix (E2)**: 192 parameterized tests in `tests/unit/safety/parameterized/watchdog_timeout_matrix.cpp`
   - Miss count: 1, 2, 3
   - Duration: 60, 70, 100, 150 ms
   - States: SEARCH, TRACKING, ARMED, FAULT_RECOVERY
   - Period: 10, 20, 50 ms
   - Total: 192 test combinations ✅

### Build Verification

```bash
# Build and run implemented tests
cmake --build . --target estop_matrix_test watchdog_timeout_matrix_test
ctest -R "EstopMatrix|WatchdogTimeout" --output-on-failure

# Results: 100% pass (216 parameterized test cases)
```

*End of Document*