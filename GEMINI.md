# GEMINI.md

This file provides comprehensive guidance to Gemini CLI when working with code in this repository. It consolidates information from various documentation sources to ensure a consistent and up-to-date understanding of the project.

## Project Overview

Aurore MkVII is a C++17 real-time vision-based turret defense system designed for the Raspberry Pi 5 platform. It processes 1536×864 RAW10 frames at 120Hz with a ≤5ms WCET budget and 1kHz safety monitoring. The system emphasizes deterministic timing, a zero-copy architecture, and comprehensive safety.

**Disclaimer:** This project is for educational and personal use only — not for safety-critical deployment.

## Core Goals

- **Real-Time Performance:** Achieve 120Hz frame processing with ≤5ms WCET budget.
- **Deterministic Timing:** Frame period tolerance of ±50μs at 99.9th percentile.
- **Safety Monitoring:** 1kHz safety monitor with comprehensive fault detection.
- **Zero-Copy Architecture:** No buffer copies between camera input and track output.

## Build Commands

Build operations are performed directly on this Raspberry Pi 5.

```bash
# Build for Raspberry Pi 5 (aarch64)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```
For native (x86_64) builds for development and testing:
```bash
# Configure
mkdir -p build-native && cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j$(nproc)
```

## Testing

Tests are executed directly on the target hardware.

```bash
# Run all unit tests
cd build && ctest --output-on-failure

# Run a single test binary directly (example)
cd build && ./ring_buffer_test
```
WCET measurement:
```bash
./scripts/wcet_analysis.sh --samples=1000000
```
Jitter monitoring (requires root for SCHED_FIFO):
```bash
sudo ./scripts/jitter_monitor.sh --duration=60
```

## Static Analysis and Formatting

```bash
# Run clang-tidy (configured as CMake target)
cd build && cmake --build . --target tidy

# Check formatting
cd build && cmake --build . --target format-check

# Apply formatting
cd build && cmake --build . --target format
```

## Architecture

### Thread Model

Four SCHED_FIFO threads pinned to specific CPUs. CPU 3 handles both the vision capture and the high-frequency safety monitor to isolate the compute-heavy track and actuation threads on CPU 2.

| Thread | Priority | CPU | Period | Phase Offset |
|--------|----------|-----|--------|--------------|
| `safety_monitor` | 99 | 3 | 1ms | 0ms |
| `actuation_output` | 95 | 2 | 8.333ms | 4ms |
| `vision_pipeline` | 90 | 3 | 8.333ms | 0ms |
| `track_compute` | 85 | 2 | 8.333ms | 2ms |

The phase offsets stagger the 120Hz threads so vision captures first, track processes 2ms later, and actuation outputs 4ms later, ensuring fresh data at each stage.

### Data Flow

```
[libcamera RAW10] → vision_pipeline → [RAW10→BGR888 conversion]
                                      ↓
                         LockFreeRingBuffer<ZeroCopyFrame, 4>
                                      ↓
                         track_compute (KCF Tracker + OrbDetector)
                                      ↓
                         LockFreeRingBuffer<TrackSolution, 4>
                                      ↓
                         actuation_output (GimbalController, Fusion HAT+ I2C)
                                      ↓
                         [HUD Socket] & [AuroreLink Protobuf]
                                      ↓
                         safety_monitor (1kHz, watches all threads)
```

### Core Primitives (`include/aurore/`)

- **`ring_buffer.hpp`** — `LockFreeRingBuffer<T, N>`: SPSC lock-free buffer, cache-line aligned.
- **`timing.hpp`** — `ThreadTiming` and `DeadlineMonitor`: uses `clock_nanosleep(TIMER_ABSTIME)` and `CLOCK_MONOTONIC_RAW`.
- **`safety_monitor.hpp`** — `SafetyMonitor`: 1kHz deadline watchdog with RAII `WatchdogKick`.
- **`state_machine.hpp`** — `StateMachine`: 7-state turret state machine (BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT).
- **`gimbal_controller.hpp`** — `GimbalController`: Converts pixel coordinates to gimbal angles with rate limiting.
- **`ballistic_solver.hpp`** — `BallisticSolver`: Computes lead angles using precomputed p_hit lookup tables (RK4 with G1 drag model).
- **`aurore_link_server.hpp`** — `AuroreLinkServer`: Protobuf-over-TCP telemetry and command server.
- **`hud_socket.hpp`** — `HudSocket`: Low-latency JSON telemetry via UNIX domain socket.
- **`camera_wrapper.hpp`** — `CameraWrapper`: Manages MIPI (primary) and USB (optical gate) streams, provides test patterns.
- **`config_loader.hpp`** — `ConfigLoader`: JSON configuration for all subsystems with dot-path access.
- **`interlock_controller.hpp`** — `InterlockController`: GPIO hardware-based safety interlock.
- **`telemetry_writer.hpp`** — `TelemetryWriter`: Asynchronous telemetry logging to CSV/JSON.

## What Is Implemented vs. TODO

**Implemented:**
- Core real-time primitives (`LockFreeRingBuffer`, `SafetyMonitor`, `ThreadTiming`).
- `StateMachine` - Full 7-state management with transition callbacks.
- `OrbDetector` & `KcfTracker` - Integrated vision pipeline with zero-copy frame handling, CLAHE preprocessing, RANSAC, and NCC redetection.
- `BallisticSolver` - RK4 with G1 drag model, Monte Carlo P(hit) estimation, mode selection (kinetic/drop).
- `FusionHat` - I2C driver for gimbal and interlock control, range gate safety.
- `HudSocket` - Low-latency telemetry for `aurore-link` frontend via UNIX domain socket.
- `AuroreLinkServer` - Remote operator interface (TCP/Protobuf telemetry and commands).
- `CameraWrapper` - Test pattern mode, frame capturing.
- `ConfigLoader` - JSON configuration for all subsystems with dot-path access.
- `InterlockController` - GPIO hardware-based safety interlock.
- `TelemetryWriter` - Asynchronous telemetry logging to CSV/JSON with backpressure.
- `GimbalController` - Pixel-to-angle conversion, dual-source (AUTO/FREECAM) support.
- AuroreLink Python client and UI - MVP for telemetry display and freecam control.
- Annotated JPEG video streaming from MkVII to Aurore Link.

**TODO:**
- `src/vision/image_preprocessor.cpp` - Implementation of standalone pre-processing stages (from `docs/plans/2026-03-06-engagement-pipeline.md`).
- Tuning of KCF parameters for high-speed dynamic targets.
- Hardening of `camera_auth` HMAC verification for production use.

## Operating Environment

- **Direct Hardware Access:** Gemini CLI operates directly on the target hardware. All development and testing utilizes the actual MIPI camera, I2C servos, and LRF.
- **Deterministic Timing:** Real-time requirements (≤5ms WCET) are verified in situ.
- **System Privileges:** Root privileges are required and available for `SCHED_FIFO` scheduling, memory locking (`mlockall`), and direct hardware access.
- **CPU Isolation:** CPUs 2–3 are isolated (`isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1`) with the CPU governor set to `performance`.
- **PREEMPT_RT Kernel:** Recommended for hard real-time performance.

## Dependency Management & Security

- **SBOM:** Comprehensive Software Bill of Materials in `docs/dependencies.md`.
- **CVE Remediation:** Minimum versions enforced for libraries like OpenCV (≥4.9.0), libwebp (≥1.3.2), glibc (≥2.39-1ubuntu3) to mitigate known CVEs.
- **Update Policy:** Security patches applied within 7 days, minor updates monthly.
- **License Compliance:** LGPL-2.1 dependencies are dynamically linked, source disclosure requirements met.

## Hardware Testing Policy

- **Real Hardware Only:** No mocks, simulations, or test pattern modes for hardware tests.
- **Immediate Failure:** Tests fail within 500ms if hardware is not detected/responding.
- **Clear Error Messages:** Failure messages identify missing hardware, connection points, and verification steps.
- **Pre-flight Check:** `scripts/check-hardware.sh` verifies hardware presence.

## Development Workflow

- **Track-Based Development:** Work organized into features, bug fixes, or refactoring efforts.
- **Code Quality:** >80% line coverage, zero clang-tidy/eslint warnings.
- **Version Control:** Conventional Commits (`type(scope): description`) and Git Notes for task summaries.

## Coding Standards

- **C++:** Google C++ Style with `.clang-format` and `.clang-tidy`. Emphasis on modern C++17, RAII, `std::atomic` with explicit memory ordering, and real-time constraints (no heap allocation in hot paths, no blocking calls). See `conductor/code_styleguides/cpp.md`.
- **JavaScript:** ES2022+ with `eslint`. Emphasis on `const`/`let`, arrow functions, `async`/`await`, DOM caching, and performance. See `conductor/code_styleguides/javascript.md`.

## Runtime Operation Details

- **Binary Execution:** Always use `sudo timeout --kill-after=1s 15s /home/pi/Aurore/build/AuroreMkVI` to run the main binary, ensuring clean termination and resource release.
- **Process Termination:** If hardware conflicts or `AuroreMkVI` failures occur, immediately use `pgrep AuroreMkVI` followed by `sudo pkill -9 <PID>` for each identified process.
- **Wi-Fi Access Point:** Raspberry Pi 5 is configured as a Wi-Fi AP (`AuroreMkVII` SSID, `aurore` password) for remote operator control via `aurore-link` (see `docs/wifi_ap_setup.md`).
- **IMU Sensor:** Integrates IMU data from an Android phone (SensaGram app) over UDP to `eth0` (see `docs/imu_sensor.md`).
- **Laser Rangefinder:** M01 LRF communicates via UART (see `docs/laser_rangefinder_part_1.txt`, `docs/laser_rangefinder_part_2.txt`).
- **Autonomous Bug Fixing:** If "WAIT", "ERROR", or "WARNING" appears in logs after a binary run, autonomously fix the root cause and reiterate until resolved. Do not increase queue capacities to fix warnings/errors.
