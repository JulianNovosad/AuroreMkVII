# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

From now on, you assume the identity of ADA: Aurore Development Agent. The tasks you assume are as follows:
1: Boilerplace generation
2: Documentation retrieval
3: Automated error fixing
4: Code optimalization
5: System design
6: Requirements and spec enforcement 
7: Verification loops
8: Core development loops
9: Debugging loops
10: Forensic audits
11: Log analysis 
12: Fixing of issues discovered in the logs

## Project Overview

Aurore MkVII is a C++17 real-time vision-based turret defense system targeting Raspberry Pi 5. The entire compute, sensor, effector, and power assembly is mounted on a 2-DOF gimbal atop a ground tripod. It processes 1536×864 RAW10 frames at 120Hz with a ≤5ms WCET budget and 1kHz safety monitoring. **Educational/personal use only — not for safety-critical deployment.**

`AuroreMkVI/` is the predecessor implementation (TFLite + Edge TPU inference). It serves as a reference for architecture patterns, not as active source code.

## Autonomous Dev Loop

`scripts/dev-loop.sh` runs an opencode session (model: `opencode/minimax-m2.5-free`) that automatically picks the highest-priority unresolved item from `docs/issue_report.md`, implements it against `spec.md`, builds, tests, commits, and updates the issue report.

```bash
./scripts/dev-loop.sh            # run one session now
./scripts/dev-loop.sh install    # install cron job (every 4 hours)
./scripts/dev-loop.sh uninstall  # remove the cron job
```

Session logs are written to `agent_logs/dev-loop-<timestamp>.log`. The rolling cron log is `agent_logs/dev-loop.log`. Check these logs to see what the autonomous agent did last, or to diagnose a failed session.

## Build Commands

```bash
# Build for Raspberry Pi 5 (aarch64) — only supported target
./scripts/build-release.sh   # Release build → build-release/
./scripts/build-debug.sh    # Debug build   → build-debug/

# Deploy to RPi 5 (reads $RPI_USER and $RPI_HOST env vars)
./scripts/deploy-to-rpi.sh

# Manual build on the Pi
./scripts/build-release.sh
```

## Testing

```bash
# Run all unit tests
cd build-release && ctest --output-on-failure

# Run a single test binary directly
cd build-release && ./ring_buffer_test
cd build-release && ./timing_test
cd build-release && ./safety_monitor_test

# WCET measurement (run on RPi 5 target)
./scripts/wcet_analysis.sh --samples=1000000

# Jitter monitoring (requires root for SCHED_FIFO)
sudo ./scripts/jitter_monitor.sh --duration=60
```

## Hardware Test Notes

All hardware is assumed connected and functional. Verify with `ls /dev/video* /dev/ttyAMA0` before debugging.

**Camera:** Primary camera is IMX708 CSI-2 via libcamera (rp1-cfe driver, /dev/video0-7). No USB webcam is connected. `integration_check` and `UsbCamera::detect()` accept both `uvcvideo` and `rp1-cfe` drivers; if no USB webcam, CSI presence is verified via `rpicam-hello --list-cameras`.

**LRF:** M01 laser rangefinder on `/dev/ttyAMA0`. Currently measures ~0.24m (object at short range). State machine range bounds are [0.5m, 5000m]; `laser_validation_test` correctly passes when readings are rejected by bounds check (hardware and validation logic are both working). `LaserRangefinderTest`, `LaserValidationTest`, and `IntegrationCheck` all share the UART and are serialized via `RESOURCE_LOCK uart_ttyAMA0` in CMake — do not remove this or parallel ctest runs will corrupt each other's frames.

**Concurrency tests:** `test_priority_inversion_mitigation` in `concurrency_pathology_test` is synchronized via a `medium_ready` flag; do not remove the mutex pre-acquisition pattern or the test will flake.

## Static Analysis and Formatting

```bash
# Run clang-tidy (configured as CMake target)
cd build-release && cmake --build . --target tidy

# Check formatting
cd build-release && cmake --build . --target format-check

# Apply formatting
cd build-release && cmake --build . --target format
```

## Architecture

### Thread Model

Four SCHED_FIFO threads pinned to specific CPUs:

| Thread | Priority | CPU | Period | Phase Offset |
|--------|----------|-----|--------|--------------|
| `safety_monitor` | 99 | 3 | 1ms | 0ms |
| `actuation_output` | 95 | 2 | 8.333ms | 4ms |
| `vision_pipeline` | 90 | 2 | 8.333ms | 0ms |
| `track_compute` | 85 | 2 | 8.333ms | 2ms |

The phase offsets stagger the 120Hz threads so vision captures first, track processes 2ms later, actuation outputs 4ms later.

### Data Flow

```
[libcamera RAW10] → vision_pipeline → [RAW10→BGR888 conversion]
                                      ↓
                         LockFreeRingBuffer<ZeroCopyFrame, 4>
                                      ↓
                                 track_compute
                                      ↓
                         LockFreeRingBuffer<TrackSolution, 4>
                                      ↓
                               actuation_output → Fusion HAT+ I2C
                                      ↓
                                safety_monitor (1kHz, watches all threads)
```

### Core Primitives (`include/aurore/`)

- **`ring_buffer.hpp`** — `LockFreeRingBuffer<T, N>`: SPSC lock-free buffer, cache-line aligned, power-of-2 size. Used for `ZeroCopyFrame` transfer between vision and track threads.
- **`timing.hpp`** — `ThreadTiming(period_ns, phase_offset_ns)`: uses `clock_nanosleep(TIMER_ABSTIME)` to prevent drift. `DeadlineMonitor` wraps individual work sections. `get_timestamp()` returns `CLOCK_MONOTONIC_RAW`.
- **`safety_monitor.hpp`** — `SafetyMonitor`: 1kHz deadline watchdog. Threads call `update_vision_frame()` / `update_actuation_frame()` each cycle; safety thread calls `run_cycle()`. `WatchdogKick` is an RAII guard for the 60ms software watchdog.
- **`camera_wrapper.hpp`** — `CameraWrapper`: libcamera + DMA mmap for zero-copy. `wrap_as_mat()` creates an OpenCV `cv::Mat` header over the DMA buffer without copying.
- **`telemetry_writer.hpp`** / **`telemetry_types.hpp`** — Binary telemetry log with typed event IDs (see `TelemetryEventId` enum in `telemetry_types.hpp`).

### What Is Implemented vs. TODO

**Implemented:**
- `LockFreeRingBuffer`, `ThreadTiming`, `DeadlineMonitor`, `SafetyMonitor`, `CameraWrapper`, `TelemetryWriter`
- `StateMachine` - 7-state turret state machine (BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT)
- `BallisticSolver` - ballistic trajectory computation with precomputed p_hit lookup tables
- `FusionHat` - I2C driver for Fusion HAT+ gimbal controller with async command queuing
- `KcfTracker` - KCF (Kernelized Correlation Filter) visual tracker (1-2ms execution time)
- `OrbDetector` - ORB feature-based target detection
- Main thread skeleton with 4-thread startup/shutdown in `src/main.cpp`
- Unit tests for ring buffer, timing, and safety monitor

**TODO stubs** (commented out in `CMakeLists.txt`):
- `src/vision/` — image preprocessor, color segmentation (ORB detector implemented, integration pending)
- `src/common/` — logger, config loader
- HUD telemetry UNIX domain socket output (TelemetryWriter implemented, socket transport pending)
- Integration tests with hardware
- Gimbal control integration in `main.cpp` (Fusion HAT+ I2C commands pending)

**Tracker Selection Rationale:**
KCF tracker is used instead of CSRT for WCET compliance:
- KCF: 1-2ms execution time at 1536×864 resolution
- CSRT: 10-20ms execution time (exceeds 5ms WCET budget)
- KCF provides sufficient accuracy for rigid target tracking at 120Hz
- Trade-off: KCF does not support scale change detection (acceptable for fixed-range targets)

## Requirements Traceability

`spec.md` is the authoritative requirements document. Requirements are tagged `AM7-L{level}-{subsystem}-{id}`. Active gaps (blocking `compliance_complete` gate) are tracked in `agent_sessions/session_20260305_001/blackboard/quality_gates.json`.

## Code Style

From `AuroreMkVI/AGENTS.md` (applies to MkVII as well):

- **Classes:** `PascalCase` — **Functions/variables:** `snake_case` — **Constants:** `kConstantName` — **Enums:** `kEnumValue`
- 4 spaces, K&R braces, 100-character soft limit
- Include order: standard library → system → project headers
- Atomics: always use explicit memory ordering (`memory_order_acquire` / `memory_order_release`)
- No heap allocation in real-time threads after init; no `memcpy()` on the critical path (zero-copy invariant)
- WCET measurements start from the 2nd invocation (first warms up caches)

## Mocks are STRICTLY PROHIBITED

**No mocks, simulations, or fakes of any kind are allowed ANYWHERE in this project.**

This is an absolute rule with zero exceptions:
- No mock objects, mock classes, mock functions, or mock anything
- No simulated hardware, simulated detectors, simulated anything
- No fake data generators, stub implementations, or test doubles
- No variables, functions, or comments containing "mock", "Mock", "simulate", or "Simulated"
- Unit tests test pure logic functions; integration tests connect to real hardware
- If hardware is needed but unavailable, the test must FAIL immediately — never degrade gracefully

If you need to test error conditions, use fault injection on real hardware or document as a hardware limitation.

## Build Target

The only supported build target is Raspberry Pi 5 (aarch64). The toolchain file is `cmake/aarch64-rpi5-toolchain.cmake` when cross-compiling from a host machine; on the Pi itself use a plain CMake configure. All tests run on the Pi. The `TimingIntegrationTest` is disabled by default in CTest — enable it only when SCHED_FIFO is available.

## Real-Time Target Configuration

On the RPi 5, CPUs 2–3 must be isolated (`isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1` in `/boot/firmware/cmdline.txt`) and CPU governor set to `performance`. Run the binary as root for `SCHED_FIFO` and `mlockall`.

## COMPLIANCE

    This project targets MISRA C++:2023 (or MISRA C++:2008). All new code must comply.
    Key rules in force:
    - Rule 0-1-1: No unreachable code
    - Rule 5-0-*: No implicit conversions
    - Rule 6-4-*: No dynamic allocation after init
    - Rule 18-4-1: No use of dynamic heap memory in RT paths
    - Use clang-tidy with `readability-*` and `cppcoreguidelines-*` checks enabled
    - Use MIL-STD-498/ DO-178C process compliance for documentation and traceability

## Hardware Context
This is a Raspberry Pi 5 / Aurore MkVII project. All performance work, NEON code, and camera/MIPI fixes must be validated on actual hardware. Never propose 'actionable without hardware' plans for perf/HW issues.

## NEON / SIMD Code Rules
Before writing any NEON intrinsics, verify each intrinsic exists in arm_neon.h (e.g., vld5_u8 does NOT exist). Compile-check incrementally rather than writing large blocks. For DMA buffer reads, assume non-cacheable memory and use vectorized loads with prefetch.

## Test & Verify Loop
After any code change: (1) build, (2) run the relevant test suite, (3) on fix-and-retry tasks, iterate until ALL tests pass before declaring done. Don't stop at compilation success.

## Session Hygiene
When approaching context/usage limits, STOP starting new investigations. Instead: commit current progress, write a STATUS.md summarizing the hypothesis/next step, and exit cleanly so the next session can resume.
