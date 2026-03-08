# GEMINI.md

This file provides guidance to Gemini when working with code in this repository.

## Project Overview

Aurore MkVII is a C++17 real-time vision-based fire control system targeting Raspberry Pi 5. It processes 1536×864 RAW10 frames at 120Hz with a ≤5ms WCET budget and 1kHz safety monitoring. **Educational/personal use only — not for safety-critical deployment.**

`AuroreMkVI/` is the predecessor implementation (TFLite + Edge TPU inference). It serves as a reference for architecture patterns, not as active source code.

## Build Commands

```bash
# Native build (laptop x86_64, for development and unit tests)
./scripts/build-native.sh [Debug|Release|RelWithDebInfo]

# Cross-compile for Raspberry Pi 5 (aarch64)
./scripts/build-rpi.sh [Debug|Release]

# Deploy to RPi 5 (reads $RPI_USER and $RPI_HOST env vars)
./scripts/deploy-to-rpi.sh

# Manual build
mkdir build-native && cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

## Testing

```bash
# Run all unit tests (native build)
cd build-native && ctest --output-on-failure

# Run a single test binary directly
cd build-native && ./ring_buffer_test
cd build-native && ./timing_test
cd build-native && ./safety_monitor_test

# WCET measurement (must run on RPi 5 target)
./scripts/wcet_analysis.sh --samples=1000000

# Jitter monitoring (requires root for SCHED_FIFO)
sudo ./scripts/jitter_monitor.sh --duration=60
```

## Static Analysis and Formatting

```bash
# Run clang-tidy (configured as CMake target)
cd build-native && cmake --build . --target tidy

# Check formatting
cd build-native && cmake --build . --target format-check

# Apply formatting
cd build-native && cmake --build . --target format
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
- `StateMachine` - 7-state FCS state machine (BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT)
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

## Cross-Compilation

The toolchain file is `cmake/aarch64-rpi5-toolchain.cmake`. Binaries that require hardware (camera, I2C, SCHED_FIFO at high priority) must run on the RPi 5. Unit tests for pure-logic modules (`ring_buffer_test`, `safety_monitor_test`) run on the native build. The `TimingIntegrationTest` is disabled by default in CTest — enable it only on the target.

## Real-Time Target Configuration

On the RPi 5, CPUs 2–3 must be isolated (`isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1` in `/boot/firmware/cmdline.txt`) and CPU governor set to `performance`. Run the binary as root for `SCHED_FIFO` and `mlockall`.
