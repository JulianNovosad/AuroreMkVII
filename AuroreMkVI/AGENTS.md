# Authoritative Agentic File

> ## ⚠️ DEPRECATED — Aurore MkVI (Edge TPU)
>
> **This document describes Aurore Mk VI, the predecessor to Aurore MkVII.**
>
> **MkVI Architecture:** TensorFlow Lite + Edge TPU inference pipeline
> **MkVII Architecture:** libcamera + OpenCV (KCF tracker, ORB detector)
>
> **Key Differences:**
> | Aspect | MkVI (Deprecated) | MkVII (Current) |
> |--------|-------------------|-----------------|
> | Inference | Edge TPU (TFLite) | Classical CV (OpenCV) |
> | Tracker | N/A | KCF (1-2ms WCET) |
> | Detector | TFLite model | ORB + RANSAC |
> | Camera | libcamera (legacy) | libcamera (zero-copy) |
> | State Machine | IDLE→ALERT→LOCATED→HOMING→LOCKED→ARMED→FIRE | BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT |
>
> **For current implementation guidance, see:**
> - `/home/laptop/AuroreMkVII/QWEN.md` — MkVII development guide
> - `/home/laptop/AuroreMkVII/README.md` — MkVII build and usage
> - `/home/laptop/AuroreMkVII/spec.md` — MkVII requirements
> - `/home/laptop/AuroreMkVII/docs/` — MkVII subsystem documentation
>
> **This file is retained for:** Architecture pattern reference, historical context, telemetry format compatibility (unified.csv)

---

## Project Overview

Aurore Mk VI is a defense-grade, safety-critical embedded system for Raspberry Pi 5. It implements a real-time, zero-copy inference pipeline with deterministic actuation.

**Platform:** Raspberry Pi 5 (4GB+) with Raspberry Pi OS Lite (64-bit)  
**Language:** C++17  
**Build Directory:** `/home/pi/Aurore/build/`

## Build Commands

```bash
# Full build
cd /home/pi/Aurore && mkdir -p build && cd build && cmake .. && make AuroreMkVI

# Build specific test (single test)
cd /home/pi/Aurore/build && make <test_name>
# Examples:
make ballistic_safety_test
make thread_affinity_test
make geometry_test
make orange_zone_test
make tpu_wcet_test

# Run built test
./src/<test_name>
# Examples:
./src/ballistic_safety_test
./src/geometry_test
./src/tpu_wcet_test
```

## Running the Application

```bash
# Always use timeout with --kill-after
sudo timeout --kill-after=1s 15s /home/pi/Aurore/build/AuroreMkVI
```

## Pipeline Architecture

```
Camera → ImageProcessor → TPU Inference → Logic → Servo Control
   ↓                                 ↓              ↓
Display                          Telemetry      Safety Monitor
```

**Key:** TPU inference path is ENABLED using Edge TPU with TensorFlow Lite delegate.

### Target Requirements
- Exactly 1 detection per frame (NOT 0, NOT >1)
- Square shape, centered horizontally
- Position: below crosshair in lower half of frame

### Module Structure
- `camera/` - libcamera capture with zero-copy DMA buffers
- `inference/` - TensorFlow Lite + Edge TPU (enabled)
- `decision/` - Safety monitor with kill-switch
- `servo/` - PCA9685 PWM controller
- `telemetry/` - System telemetry collection

## Code Style Guidelines

### Naming Conventions
- **Classes:** `PascalCase` (e.g., `Application`, `CameraCapture`)
- **Functions:** `snake_case` (e.g., `initialize()`, `process_frame()`)
- **Variables:** `snake_case` (e.g., `frame_count`, `detection_bbox`)
- **Constants:** `kConstantName` (e.g., `kMaxQueueSize`)
- **Enums:** `PascalCase` with `k` prefix for values (e.g., `enum class State { kIdle, kRunning }`)

### Formatting
- **Indentation:** 4 spaces (no tabs)
- **Line length:** Soft limit 100 characters
- **Braces:** K&R style (opening brace on same line)
- **Includes:** Standard library first, then project headers
  ```cpp
  #include <vector>
  #include <memory>
  #include "application.h"
  #include "config_loader.h"
  ```

### Types
- Use `std::chrono::steady_clock` for timestamps
- Use `int64_t` for millisecond timestamps
- Use `float` for coordinates and calculations
- Use `size_t` for sizes and counts

### Imports
- Always verify headers exist before using functions
- Use forward declarations to break circular dependencies
- Document verified headers with comment: `// Verified headers: [header1, header2...]`

### Error Handling
- Use `APP_LOG_ERROR()`, `APP_LOG_WARN()`, `APP_LOG_INFO()` (see `util_logging.h`)
- Return error codes from critical paths
- Validate all external inputs (config, sensor data)
- Exceptions only acceptable in initialization

### Thread Safety
- Use atomics with explicit memory ordering (`memory_order_acquire`, `memory_order_release`)
- Prefer lock-free queues (`LockFreeQueue<T>`) over mutexes in hot paths
- Document threading model in module headers

### TPU Optimization
- Model is pre-compiled for Edge TPU (single-batch only, no quantization needed)
- Pre/post-processing may occur outside `inference/` module - optimize wherever it happens
- Focus on optimizing your own code in `src/` (not the model)
- Minimize CPU-TPU data transfers
- Use DMA buffers for zero-copy paths

### Real-Time & WCET
- All inference paths must meet deterministic latency requirements
- Profile and measure Worst Case Execution Time (WCET) for TPU inference
- **Important:** First inference is slower (model loads into TPU DRAM) - measure WCET from 2nd inference onwards
- Sample size: 10,000 inferences (excluding warm-up)
- Ensure frame processing completes within frame budget (e.g., 33ms at 30fps)
- Use lock-free queues to avoid priority inversion
- Run `tpu_wcet_test` to validate timing compliance

## Key Design Patterns

**Zero-Copy:** `PooledBuffer<T>` with reference counting, DMA file descriptors  
**Lock-Free:** `LockFreeQueue<T>`, `TripleBuffer<T>` for producer-consumer sync  
**Safety:** Global `g_running` atomic, 10s watchdog for graceful shutdown

## Important Files

- `src/main.cpp` - Entry point with signal handling and watchdog
- `src/application.h/cpp` - Main application class
- `src/pipeline_structs.h` - Core data structures
- `src/logic.h/cpp` - Decision logic and servo control
- `src/util_logging.h` - Logging infrastructure

## Critical Rules

1. **No stubs/simulations** - All code must use real hardware
2. **No queue cap increases** - Fix root cause instead
3. **Post-run verification** - Always check both the `run.json` and `unified.csv` inside of `logs/session*/*` for ERROR/WARNING/WAIT or any other issue or anomaly
4. **Process cleanup** - Run `pgrep AuroreMkVI` and `sudo pkill -9` on conflicts
5. **Hardware verify** - Check `/proc/device-tree/model` shows Pi 5

## Conductor Integration

Record all actions in active track (`conductor/tracks.md`). Update `plan.md` on task completion.

## Dependencies

All dependencies vendored in `artifacts/` and `deps/`. Verify before rebuilding:
```bash
ls -la artifacts/<lib>/lib/
nm -D artifacts/<lib>/lib/<lib>.so | head -20
```
