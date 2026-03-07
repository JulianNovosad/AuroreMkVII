# QWEN.md

This file provides guidance to QWEN when working with code in this repository.

## Project Overview

Aurore Mk VI is a defense-grade, safety-critical embedded system for Raspberry Pi 5. It implements a real-time, zero-copy inference pipeline with deterministic actuation, strict auditing, and PREEMPT_RT kernel requirements.

**Target Platform:** Raspberry Pi 5 (4GB+) with Raspberry Pi OS Lite (64-bit)

**Language:** C++17

**Safety-Critical Nature:** This is a safety-critical system. Changes must maintain deterministic behavior, real-time guarantees, and strict resource management.

## Build System



1. **Kernel** - Custom PREEMPT_RT patched kernel (rpi-6.12.y branch)
2. **Gasket Driver** - Coral TPU driver (apex_rpi5.ko, gasket_rpi5.ko)
3. **libedgetpu** - Google Coral Edge TPU runtime
4. **TensorFlow Lite** - Inference runtime with delegate support
5. **libcamera** - Raspberry Pi camera pipeline (rpi/vc4, rpi/pisp)
6. **FFmpeg** - Video encoding with V4L2 M2M hardware acceleration
7. **Aurore Core** - Main application binary

## Architecture

**Note:** The TPU inference path is **DISABLED** and should NEVER be enabled. The system uses GPU-native OpenCV-based concentric ring detection (bullseye targets) for inference via the GPU compute pipeline.

### Target Setup

A single physical square target has been placed for camera detection:
- **Shape:** Square (width = height)
- **Position:** Below the crosshair, vertically in the lower half of the frame
- **Size:** Height = 1/2 of total frame height (720/2 = 360 pixels at 1280x720 resolution)
- **Expected:** Exactly 1 detection per frame

### Detection Requirements

The GPU/OpenCV detection pipeline MUST produce:
1. **Exactly 1 detection** per frame (NOT 0, NOT more than 1)
2. **Bounding box** centered horizontally in the frame
3. **Bounding box** positioned below the crosshair (upper part of lower half)
4. **Square aspect ratio** matching the physical target

**Debugging rule:** If 0 detections or >1 detections are logged, the detection code is wrong and needs fixing.

### Pipeline Overview

Aurore implements a **zero-copy, lock-free pipeline** with the following stages:

```
Camera → ImageProcessor → TPU Inference → Logic/Decision → Servo Control
   ↓                                ↓                           ↓
Display                        Telemetry                  Safety Monitor
   ↓
```

### Module Structure



- **camera/** - libcamera capture with zero-copy DMA buffer support
- **inference/** - TensorFlow Lite + Edge TPU inference engine
- **decision/** - Safety monitor with kill-switch integration
- **servo/** - PCA9685 PWM controller for servo actuation
- **telemetry/** - System and pipeline telemetry collection
- **monitor/** - Terminal UI for real-time monitoring

### Key Design Patterns

**Zero-Copy Architecture:**
- Uses `PooledBuffer<T>` with reference counting to avoid memcpy
- Frames carry DMA file descriptors (`fd`, `offset`, `length`) for direct hardware access
- Triple buffering (`TripleBuffer<T>`) for producer-consumer sync without blocking

**Lock-Free Queues:**
- `LockFreeQueue<T>` for inter-thread communication
- Atomic operations with explicit memory ordering
- Queues defined in `pipeline_structs.h`: `ImageQueue`, `DetectionResultsQueue`, `H264Queue`

**Deterministic Timing:**
- All timestamps use `std::chrono::steady_clock`
- Authoritative raw timestamps via `get_time_raw_ms()` (see `timing.h`)
- Per-frame latency accounting (capture → preprocess → inference → encode → display)

**Safety-Critical Shutdown:**
- Global `g_running` atomic flag with acquire/release semantics
- Hard-kill watchdog thread (10s timeout) forces `_exit(1)` if graceful shutdown fails
- `ApplicationSupervisor` manages module lifecycle and ensures proper cleanup order

## Configuration

Configuration is loaded from JSON files via `ConfigLoader`. Key configuration includes:
- Camera resolution and framerate
- TPU model paths and inference parameters
- Servo channel mappings and limits
- Network streaming endpoints
- Safety thresholds

## Important Files


- **src/main.cpp** - Entry point with signal handling and watchdog
- **src/application.h/cpp** - Main application class that wires all modules together
- **src/pipeline_structs.h** - Core data structures (ImageData, DetectionResults, queues)
- **src/logic.h/cpp** - Decision logic with ballistic calculations and servo control
- **.clang-tidy** - Static analysis configuration

## Artifacts

Build artifacts are collected in `artifacts/`:
- `kernel/Image` - Kernel image
- `kernel/*.dtb` - Device tree blobs
- `kernel/modules/*.ko` - Kernel modules
- `libcamera/`, `tflite/`, `ffmpeg/`, `libedgetpu/` - Dependency libraries
- `checksums.txt` - SHA256 checksums of all artifacts

## Dependencies

All dependencies are vendored or built from source in `deps/`.

## Development Guidelines

**Thread Safety:**
- Use atomic operations with explicit memory ordering (`memory_order_acquire`, `memory_order_release`)
- Prefer lock-free queues over mutexes in hot paths
- Document threading model in module headers

**Code Restrictions:**
- Simulations and stubs are absolutely forbidden
- All functionality must use real hardware interfaces
- No mock implementations for safety-critical components

**Issue Resolution Process:**
- Always investigate the actual root cause by searching the codebase and running diagnostic commands
- Identify the specific code location causing the issue
- Fix the issue with targeted changes
- Recompile and rerun the binary to verify the fix works

**Resource Management:**
- Always use RAII (smart pointers, destructors)
- Edge TPU delegates must be freed before TF Lite interpreter
- Camera resources must be released before libcamera shutdown

**Error Handling:**
- Log errors via `APP_LOG_ERROR()`, `APP_LOG_WARN()`, `APP_LOG_INFO()` (see `util_logging.h`)
- Return error codes from critical paths; exceptions are acceptable in initialization
- Validate all external inputs (config, sensor data, network packets)

**Reproducibility:**

- All artifacts are checksummed
- Compiler flags strip build paths (`-fdebug-prefix-map`)

## Dependency Verification Protocol

**Before building or reconfiguring any dependency:**
1. Check if the dependency already exists in `artifacts/` or `deps/`
2. Verify installation with appropriate tool:
   - Libraries: `ls -la artifacts/<libname>/lib/` and `nm -D artifacts/<libname>/lib/<libname>.so | head -20`
   - Kernel modules: `ls -la kernel/modules/*.ko`
   - Binaries: Check PATH and run `--version`
3. If existing and functional, DO NOT rebuild
4. Document verification in task notes before proceeding

**No Python in Runtime or Tests:**
- All production code and tests MUST be C++17
- No Python scripts for runtime functionality
- No Python-based test frameworks
- Build system (CMake) is acceptable for configuration only

## Verification Checklist

For each task involving external dependencies:
- [ ] Locate existing installation (search artifacts/, deps/, system)
- [ ] Verify symbols/version with `nm`, `ldd`, or appropriate tool
- [ ] Test basic functionality if applicable
- [ ] Document findings in task notes
- [ ] Proceed only if verification fails (existing + functional = skip)

## Hardware Requirements

- **Raspberry Pi 5** with active cooling
- **Coral M.2 Accelerator**
- **Raspberry Pi Camera Module 3**
- **PCA9685 PWM board**
- **Kill switch** on HAT (disables power to the entire HAT)

## Hardware Verification

**All development and testing is performed on actual Raspberry Pi 5 hardware, NOT in simulation or emulation.**

Before running or debugging issues, verify the hardware environment:

```bash
# Verify Raspberry Pi 5 model
cat /proc/device-tree/model

# Check Linux kernel version (should be PREEMPT_RT)
uname -a

# List PCI devices (Coral TPU shows as PCIe device)
lspci | grep -i tpu

# Check video devices (camera)
ls -la /dev/video0

# Check GPU/display devices
ls -la /dev/dri/

# Check CPU info
cat /proc/cpuinfo | grep "model name" | head -1
```

If running on incorrect hardware (x86 instead of ARM), the build or runtime will fail. Always verify with `uname -m` should show `aarch64`.

## Kernel Configuration

The system requires a PREEMPT_RT kernel. The build verifies `CONFIG_PREEMPT_RT=y` in the kernel config. Pinned kernel commit and config are stored in:
- `kernel/config/pinned.commit`
- `kernel/config/pinned.config`

Kernel patches are applied from `patches/kernel/*.patch`.

## Logging

Logs are written to `logs/` directory with automatic rotation via `scripts/log_rotate.sh`. Logger is initialized in main with:

```cpp
Logger::init("run", "logs", nullptr);
Logger::getInstance().start_writer_thread();
```
- The final executable is at `/home/pi/Aurore/build/AuroreMkVI`
- - Always run the binary with `sudo timeout --kill-after=1s 15s` for testing (e.g., `sudo timeout --kill-after=1s 15s /home/pi/Aurore/build/AuroreMkVI`). NEVER use extra commands or pipes when running the binary
- All builds must take place in `/home/pi/Aurore/build/` directory:
  ```bash
  cd /home/pi/Aurore && mkdir -p build && cd build && cmake .. && make AuroreMkVI
  ```

## Conductor Integration

This project uses Conductor for context-driven development. **Every action must be recorded** in the active conductor track:

1. When working on any task, identify the active track in `conductor/tracks.md`
2. After completing significant work (code changes, tests, fixes), update the track's `plan.md` to mark tasks as complete
3. Record all actions taken: code modifications, test results, verification steps, and any issues encountered
4. Update the track's `spec.md` if requirements or implementation details change
5. Before starting new work, check `conductor/tracks.md` to see the current project status and active track
6. When creating new features or fixing bugs, use the Conductor new track protocol to create a proper spec and plan

**Active track**: Check `conductor/tracks.md` for the current track being worked on.

## Qwen Added Memories
- **Mandatory Timeout Usage (Persistent Memory Addition)**: The `timeout` command **must always** be used with `--kill-after` (e.g., `--kill-after=1s`) when running `AuroreMkVI` to ensure clean process termination. Never use `timeout` without this flag.

- ## **Mandatory Process Termination Rule (Persistent Memory Addition)**
  **On any hardware occupation issue or when `AuroreMkVI` fails to start due to resource conflicts:**
  The agent **must immediately** run `pgrep AuroreMkVI` to identify any lingering processes, and if found, execute `sudo pkill -9 <PID>` for each identified process.

### Required procedure
  Run `pgrep AuroreMkVI`. If PIDs are returned, for each PID:
  ```bash
  sudo pkill -9 AuroreMkVI
  ```
### Rationale
  Ensures clean termination of the primary application to free critical hardware resources like the camera or framebuffer, preventing "Device or resource busy" errors.
- always run the binary after compiling to make sure it behaves like it needs to. Checking happens by reading the logs/session*/* (both run.json and unified.csv) and comparing it to the initial requirements from the user prompt
- after every run of the binary, grep the logs for "WAIT", "ERROR", "WARNING", and autonomously fix them as a "while i am here"
- Do not increase queue caps, instead fix the root cause
- if you encounter a warning, error or "WAIT" in the logs, keep fixing the source code at their root cause level and reiterate until all issues are actually fixed
