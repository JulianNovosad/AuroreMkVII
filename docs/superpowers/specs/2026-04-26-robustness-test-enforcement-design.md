# Robustness Test Suite & CI Enforcement Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Aurore MkVII's test suite with 10 new robustness test families (Parts 1–10) and a 5-tier CI system that hard-fails on hardware absence rather than skipping.

**Architecture:** Pure-logic tests run in Tiers 0–2 via CTest label filtering; all tests share a common `TestInfrastructure` library (`include/aurore/test_infrastructure.hpp` + `src/test_infrastructure.cpp`) providing fake-free instrumentation classes. Hardware-dependent tests are Tier 4, triggered manually only, and fail immediately when hardware is absent.

**Tech Stack:** C++17, CTest label-based tier filtering, GitHub Actions matrix strategy, `SafetyMonitor` + `StateMachine` + `LockFreeRingBuffer` as production SUT.

---

## Design Decisions

### No Mocks
Every test exercises production code directly. `TestInfrastructure` classes (`LogTester`, `OffsetTracker`, `FaultTimeline`, `CrashDumpTester`, etc.) are thin wrappers that instrument real subsystem calls — they carry no simulated behavior.

### Hardware Absence = Hard Fail
Tier 4 tests open real device nodes (I2C, camera, GPIO). If the device is absent, `open()` / `libcamera` initialization returns an error and the test binary exits with code 1 immediately. No graceful skip path exists.

### SafetyMonitor Arming Protocol
`check_vision_health` only detects stalls when `current_count > 0`. Tests that need stall detection must:
1. Call `update_vision_frame()` once to arm the watchdog.
2. Set `vision_deadline_ns` to a sub-millisecond value.
3. Sleep ≥ 5 ms so the stall window elapses before `run_cycle()`.

### Fault Code Stability
`WATCHDOG_FEED_FAILED` can be overwritten by `CONSECUTIVE_DEADLINE_MISSES` once the miss counter reaches `max_consecutive_misses`. Tests that assert on fault identity must set `max_consecutive_misses = 100` to prevent the overwrite race.

### State Machine ARMED Gate
Reaching ARMED requires all five conditions simultaneously: `redetection_score >= 0.95`, `has_stable_timing()`, `has_zero_faults()`, `has_operator_authorization()`, `p_hit >= 0.95`. Tests use `force_state_for_test()` + `on_redetection_score(0.96f)` to bypass the detection pipeline while still validating gate logic.

---

## Test Families and Files

| Part | Family | File |
|------|--------|------|
| 1 | Memory & Resource Stability | `tests/unit/memory_resource_test.cpp` |
| 2 | State & Mode Integrity | `tests/unit/state_mode_integrity_test.cpp` |
| 3 | Fault Containment | `tests/unit/fault_containment_test.cpp` |
| 4 | Concurrency Pathology | `tests/unit/concurrency_pathology_test.cpp` |
| 5 | Temporal Consistency | `tests/unit/temporal_consistency_test.cpp` |
| 6 | Numeric Robustness | `tests/unit/numeric_robustness_test.cpp` |
| 7 | Hostile Input | `tests/unit/hostile_input_test.cpp` |
| 8 | Resource Exhaustion | `tests/unit/resource_exhaustion_test.cpp` |
| 9 | Reset & Recovery | `tests/unit/reset_recovery_test.cpp` |
| 10 | Observability & Forensics | `tests/unit/observability_test.cpp` |

Shared infrastructure:
- `include/aurore/test_infrastructure.hpp` — all instrumentation class declarations
- `src/test_infrastructure.cpp` — implementations

---

## CI Tier Matrix

| Tier | Label | Scope | Blocks Merge | Timeout |
|------|-------|-------|-------------|---------|
| 0 | `tier0` | Fast unit: ring buffer, timing, memory, numeric | YES | 30 s |
| 1 | `tier1` | Safety & state: state machine, fault containment, concurrency, safety monitor | YES | 60 s |
| 2 | `tier2` | RT & temporal: temporal consistency, observability | YES | 60 s |
| 3 | `tier3` | Stress & soak: ballistics stress, state machine stress, core stress | NO | 300 s |
| 4 | `tier4` | HIL: gimbal actuation, boresight convergence, integration check | Manual | Manual |

CTest label syntax: `ctest -L tier0`, `ctest -L "tier0|tier1|tier2"`.

---

## Key Invariants Encoded in Tests

1. **Watchdog fires within one check interval**: `watchdog_timeout_ms=1`, sleep 100 ms → fault present in `current_fault()` without calling `run_cycle()`.
2. **Stall detection arms on first frame**: zero-frame monitors never raise VISION_STALLED or ACTUATION_STALLED.
3. **SEARCH→FREECAM is invalid**: `request_freecam()` only accepts `IDLE_SAFE` as source per AM7-L3-MODE-001.
4. **ARMED gate is atomic**: all five conditions must hold at the same `on_ballistics_solution()` call.
5. **Ring buffer capacity is N-1**: `LockFreeRingBuffer<T, N>` holds N-1 items; tests use buffer sizes ≥ 8 when pushing 4+ items.
6. **`update_vision_frame` timestamp is ignored**: the function always stores `get_timestamp(now)` — passing an artificial old timestamp has no effect.
