# Test Family Definitions and Parameter Matrices

## Overview

Aurore MkVII implements a comprehensive tiered test system validating full-system robustness, temporal correctness, memory stability, and fault containment for a safety-critical embedded platform.

## Test Categories by Part

### Part 1: Code Logic, Memory, and Resource Stability Tests

| Test Family | File | Coverage |
|-----------|------|---------|
| Stack Integrity | `memory_resource_test.cpp` | Per-thread stack high-water marks, recursion leak detection |
| Heap Integrity | `memory_resource_test.cpp` | Allocation tracking, fragmentation, long-run stability |
| DMA Health | `memory_resource_test.cpp` | Buffer integrity, alignment, fault injection, recovery |
| Thermal/Power | `memory_resource_test.cpp` | Throttling transitions, frequency scaling timing contracts |
| Queue Stress | `memory_resource_test.cpp` | Saturation, backpressure, head-of-line blocking |

### Part 2: State and Mode Integrity

| Test Family | File | Coverage |
|-----------|------|---------|
| Valid Transitions | `state_mode_integrity_test.cpp` | All valid state/mode transitions |
| Invalid Transitions | `state_mode_integrity_test.cpp` | Rejection of invalid transitions |
| Aborted Transitions | `state_mode_integrity_test.cpp` | Partial/aborted transition handling |
| Reset Behavior | `state_mode_integrity_test.cpp` | Warm vs cold reset, brown-out recovery |
| Concurrent State | `state_mode_integrity_test.cpp` | Thread-safe state access |

### Part 3: Fault Containment and Degradation

| Test Family | File | Coverage |
|-----------|------|---------|
| Single-fault Isolation | `fault_containment_test.cpp` | No cascade between subsystems |
| Graceful Degradation | `fault_containment_test.cpp` | Partial failure handling |
| Fail-safe vs Silent | `fault_containment_test.cpp` | Correct failure mode selection |
| Storming Faults | `fault_containment_test.cpp` | Repeated fault rate limiting |

### Part 4: Advanced Concurrency Pathology

| Test Family | File | Coverage |
|-----------|------|---------|
| Deadlock Detection | `concurrency_pathology_test.cpp` | Circular wait detection |
| Livelock Detection | `concurrency_pathology_test.cpp` | Continuous retry detection |
| Starvation Detection | `concurrency_pathology_test.cpp` | Unfair scheduling detection |
| Priority Inversion | `concurrency_pathology_test.cpp` | ISR-induced inversion detection |

### Part 5: Temporal Consistency

| Test Family | File | Coverage |
|-----------|------|---------|
| Timestamp Monotonicity | `temporal_consistency_test.cpp` | Non-decreasing timestamps |
| Phase Alignment | `temporal_consistency_test.cpp` | Sensor→compute→actuation phase |
| Jitter Accumulation | `temporal_consistency_test.cpp` | Long-run jitter drift |
| Deadline Miss Behavior | `temporal_consistency_test.cpp` | Tolerated vs safety-triggering |

### Part 6: Numeric Robustness

| Test Family | File | Coverage |
|-----------|------|---------|
| Overflow/Underflow | `numeric_robustness_test.cpp` | Saturating arithmetic |
| NaN/Inf Propagation | `numeric_robustness_test.cpp` | Special value handling |
| Precision Decay | `numeric_robustness_test.cpp` | Long-running calculation precision |
| Angle/Range Wrapping | `numeric_robustness_test.cpp` | Boundary value handling |

### Part 7: Hostile Input

| Test Family | File | Coverage |
|-----------|------|---------|
| Truncated Packets | `hostile_input_test.cpp` | Partial message handling |
| Duplicated Packets | `hostile_input_test.cpp` | Deduplication |
| Delayed/Replayed | `hostile_input_test.cpp` | Sequence validation |
| Extreme Values | `hostile_input_test.cpp` | Boundary value injection |
| Conflicting Authority | `hostile_input_test.cpp` | Local vs remote authority |

### Part 8: Resource Exhaustion

| Test Family | File | Coverage |
|-----------|------|---------|
| Handle Leaks | `resource_exhaustion_test.cpp` | File descriptor tracking |
| DMA Exhaustion | `resource_exhaustion_test.cpp` | Channel pool exhaustion |
| Queue Saturation | `resource_exhaustion_test.cpp` | Backpressure propagation |

### Part 9: Reset and Recovery

| Test Family | File | Coverage |
|-----------|------|---------|
| Mid-operation Reset | `reset_recovery_test.cpp` | Reset during active operation |
| Partial Restart | `reset_recovery_test.cpp` | Subsystem-level restart |
| Config Reload | `reset_recovery_test.cpp` | Live configuration update |
| Rollback | `reset_recovery_test.cpp` | Last-known-good recovery |

### Part 10: Observability

| Test Family | File | Coverage |
|-----------|------|---------|
| Log Completeness | `observability_test.cpp` | Event logging under stress |
| Timestamp Accuracy | `observability_test.cpp` | High-precision timestamping |
| Crash Dump Integrity | `observability_test.cpp` | Post-fault forensic data |

## CI Tier Matrix

| Tier | Scope | Tests | Blocks Merge | Timeout |
|------|-------|-------|-------------|---------|
| 0 | Fast unit & logic | `RingBufferTest`, `TimingTest`, `MemoryResourceTest`, `NumericRobustnessTest` | YES | 30s |
| 1 | Safety & state | `StateModeIntegrityTest`, `FaultContainmentTest`, `ConcurrencyPathologyTest`, `SafetyMonitorTest`, `StateMachineTest` | YES | 60s |
| 2 | RT & temporal | `TemporalConsistencyTest`, `BallisticsTest`, `GimbalControllerTest` | YES | 60s |
| 3 | Stress & soak | `StressTest`, `SoakTest`, `BallisticsStressTest`, `StateMachineStressTest`, `CoreStressTest` | NO | 300s |
| 4 | HIL | `GimbalActuationTest`, `BoresightConvergenceTest`, `IntegrationCheck` | Manual | Manual |

## Test Parameter Matrices

### Safety Monitor Matrix

| Parameter | Values |
|-----------|-------|
| Missed frames | 1, 2, 3 |
| Duration (ms) | 60, 70, 100, 150 |
| State | BOOT, IDLE_SAFE, SEARCH, TRACKING, ARMED, FAULT_RECOVERY |
| Period (ms) | 10, 20, 50 |

### State Transition Matrix

| From State | Valid Events | Invalid Events |
|------------|-------------|-----------------|
| BOOT | RESET_HOME | SEARCH, TRACKING, ARMED, FREECAM |
| IDLE_SAFE | SEARCH, FREECAM | TRACKING, ARMED |
| SEARCH | TRACKING, TARGET_LOST, FAULT | FREECAM (blocked), ARMED |
| TRACKING | ARMED, TARGET_LOST, FAULT | SEARCH, FREECAM |
| ARMED | FIRE, TARGET_LOST, FAULT | Any except FAULT |
| FAULT | RESET_HOME | All except RESET_HOME |

### Watchdog Timeout Matrix

| Missed Count | Duration (ms) | Period (ms) | Expected Behavior |
|-------------|---------------|-------------|------------------|
| 1 | 60 | 10 | Warning |
| 1 | 60 | 20 | Warning |
| 1 | 60 | 50 | OK |
| 2 | 100 | 10 | Warning |
| 2 | 100 | 20 | Warning |
| 3 | 150 | 10 | FAULT |
| 3 | 150 | 20 | FAULT |

## Enforcement Rules

### Regression Prevention

1. **Test Deletion Blocks Merge** - No regression tests may be deleted
2. **Test Modification Requires Approval** - Changes to test logic require PR approval
3. **Flakiness Is Safety Fault** - Unstable tests must be fixed, not disabled

### Timing Enforcement

| Violation Type | Tolerance | Action |
|----------------|-----------|--------|
| Jitter > 1ms | 1% samples | Warning |
| Jitter > 1ms | > 5% samples | FAIL |
| Deadline miss | Tolerated | Warning |
| Deadline miss | Safety-triggering | FAIL |

### Resource Enforcement

| Resource | Threshold | Action |
|---------|-----------|--------|
| Stack margin | < 128KB | FAIL |
| Heap growth | > baseline × 2 | FAIL |
| Handle leaks | > 10/second | FAIL |
| Queue drop | > 1% | Warning |

## Test Infrastructure Reference

### Instrumentation Classes

| Class | Purpose |
|-------|---------|
| `StackTracker` | Per-thread stack high-water monitoring |
| `HeapTracker` | Heap allocation and fragmentation tracking |
| `DmaHealthMonitor` | DMA buffer health and fault injection |
| `ThermalHealthMonitor` | Thermal throttling state machine |
| `ResourceMonitor` | System resource exhaustion tracking |
| `QueueStressTest` | Queue saturation and backpressure testing |
| `ConcurrencyPathologyDetector` | Deadlock/livelock/starvation detection |
| `TimestampValidator` | Timestamp monotonicity validation |

### Test Infrastructure Location

- Header: `include/aurore/test_infrastructure.hpp`
- Implementation: `src/test_infrastructure.cpp`
- Tests: `tests/unit/*.cpp`

## Running Tests

```bash
# All tests
cd build-test && ctest --output-on-failure

# By tier
cd build-test && ctest -R "Tier0" --output-on-failure
cd build-test && ctest -R "Tier1" --output-on-failure
cd build-test && ctest -R "Tier2" --output-on-failure

# Individual test
cd build-test && ./MemoryResourceTest
cd build-test && ./StateModeIntegrityTest
cd build-test && ./FaultContainmentTest
```

## Expected Test Results

| Metric | Target | Minimum |
|--------|-------|---------|
| Tests passing | 100% | 95% |
| Test execution time (Tier 0-2) | < 60s | < 120s |
| Flakiness rate | 0% | < 1% |
| Code coverage | > 80% | > 70% |

---

## Additional Test Matrices and Enforcement Rules

### Part 11: CI Tier Enforcement

#### Tier 0: Fast Unit & Logic (BLOCKS MERGE)

Tests:
- `RingBufferTest` - Basic SPSC ring buffer correctness
- `TimingTest` - Thread timing and deadline monitoring
- `MemoryResourceTest` - Stack/heap/DMA/thermal health
- `NumericRobustnessTest` - Overflow/NaN/Inf handling

**Failure Envelope**: Max 0 failures - ANY failure blocks merge

#### Tier 1: Safety & State (BLOCKS MERGE)

Tests:
- `StateModeIntegrityTest` - Complete state transition coverage
- `FaultContainmentTest` - Single-fault isolation, graceful degradation
- `ConcurrencyPathologyTest` - Deadlock/livelock/starvation detection
- `SafetyMonitorTest` - Watchdog and health monitoring
- `StateMachineTest` - State machine logic

**Failure Envelope**: Max 0 failures - ANY failure blocks merge

#### Tier 2: RT & Temporal (BLOCKS MERGE)

Tests:
- `TemporalConsistencyTest` - Phase alignment, jitter, deadline miss behavior
- `BallisticsTest` - Ballistic solver timing
- `GimbalControllerTest` - Gimbal command timing

**Note**: Some tests may skip on non-RT kernels - this is expected behavior
**Failure Envelope**: Max 0 failures - ANY failure blocks merge

#### Tier 3: Stress & Soak (NIGHTLY ONLY)

Tests:
- `StressTest`, `SoakTest`, `BallisticsStressTest`, `StateMachineStressTest`

**Does NOT block merge** - Runs nightly only

#### Tier 4: Hardware-In-the-Loop (MANUAL)

Tests:
- `GimbalActuationTest`, `BoresightConvergenceTest`, `IntegrationCheck`

**Requires explicit trigger** - Manual only

---

### Test Naming Conventions

| Prefix | Meaning | Example |
|---------|---------|---------|
| test_ | Unit test | test_stack_tracker_basic |
| stress_ | Stress test | stress_high_frequency |
| integration_ | Integration test | integration_pipeline |
| param_ | Parameterized | param_deadline_matrix |

---

### Regression Prevention Policy

1. **Test deletion is NEVER allowed** - Tests in the registry cannot be removed
2. **Test modification requires approval** - PR review required for test logic changes
3. **Flakiness = safety fault** - Unstable tests must be fixed, not skipped
4. **Resource leaks = safety fault** - Treated as safety-critical
5. **Timing drift = safety fault** - Treated as safety-critical

### Fault Injection Policy

| Fault Type | Injection Method | Pass Criteria |
|-----------|------------------|--------------|
| Camera timeout | Simulated delay | FAULT state entered |
| Gimbal timeout | Simulated delay | FAULT state entered |
| Sequence gap | Drop frames | Detected and tracked |
| DMA error | Inject fault | Recoverable |

---

### WCET Validation

| Operation | Target WCET | Maximum |
|-----------|-------------|---------|
| Vision pipeline | < 5ms | 10ms |
| Track compute | < 2ms | 5ms |
| Actuation output | < 2ms | 5ms |
| Safety monitor cycle | < 0.5ms | 1ms |

---

### Test Execution Requirements

Every test must:
1. Output pass/fail status with test name
2. Display measured assertion values
3. Include execution timestamp
4. Provide clear failure reason on failure

### Failure Classification

| Failure Type | Severity | CI Action |
|-------------|----------|----------|
| Logic regression | CRITICAL | BLOCK |
| Safety regression | CRITICAL | BLOCK |
| Timing regression | HIGH | BLOCK |
| Resource leak | HIGH | BLOCK |
| Flakiness | HIGH | FIX REQUIRED |
| Integration | MEDIUM | REVIEW |
| Stress/Soak | LOW | NIGHTLY |