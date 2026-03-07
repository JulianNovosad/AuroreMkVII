# Authoritative agentic file

This file provides guidance to AI agents when working with code in this repository.

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
- " --- ## Core Principles Section ```markdown ## Coding Standards - **NEVER assume function signatures** — always verify against actual system headers - **NEVER use deprecated or non-standard extensions** unless explicitly requested - **ALWAYS prefer POSIX-compliant implementations** for portability - **ALWAYS include feature test macros** (_GNU_SOURCE, _POSIX_C_SOURCE, etc.) when needed - **NEVER hardcode system-specific paths** (/usr/lib, /etc, etc.) ``` --- ## System Discovery Commands Include these so the AI knows how to introspect your actual system: ```markdown ## System Header Discovery Before writing code that uses system libraries, run these commands to verify availability: ### Check Standard Headers ```bash ls /usr/include/ | grep -E "^(stdio|stdlib|string|unistd|fcntl|sys|net|arpa|netinet)" | head -20 ``` ### Check Specific Function Availability ```bash man 3 <function_name> 2>/dev/null || man 2 <function_name> 2>/dev/null || echo "Function not in man pages" ``` ### Check Library Versions ```bash # glibc version ldd --version | head -1 # Kernel headers uname -r # pkg-config for external libs pkg-config --exists <lib> && pkg-config --modversion <lib> || echo "Library not found" ``` ### Extract Actual Function Signatures ```bash # Method 1: grep headers directly grep -r "func_name" /usr/include/ 2>/dev/null | head -5 # Method 2: use compiler to dump preprocessed output echo '#include <header.h>' | gcc -E -xc - | grep -A2 "func_name" ``` ### Verify with Compiler ```bash # Create minimal test file cat > /tmp/test_func.c << 'EOF' #include <header.h> int main() { // Attempt to use function with explicit signature check return 0; } EOF gcc -Wall -Wextra -Werror /tmp/test_func.c -o /tmp/test_func 2>&1 ``` ``` --- ## Mandatory Verification Workflow ```markdown ## Required Workflow for System API Usage 1. **DISCOVER**: Run discovery commands to confirm header exists and function is declared 2. **EXTRACT**: Get actual function signature from system headers (not memory) 3. **VERIFY**: Create minimal compile test before implementing full logic 4. **IMPLEMENT**: Write code using verified signatures only 5. **VALIDATE**: Ensure code compiles with `-Wall -Wextra -Werror -pedantic` ## Forbidden Patterns - Do NOT use functions without verifying they exist in system headers - Do NOT assume errno values (check <errno.h>) - Do NOT assume struct field names (verify in headers) - Do NOT use Linux-specific features without #ifdef __linux__ guards - Do NOT use BSD extensions without #ifdef __FreeBSD__ / __OpenBSD__ / __NetBSD__ guards ``` --- ## Platform-Specific Sections ```markdown ## Target Platform Constraints ### If Targeting Linux Only - Kernel version: [YOUR_VERSION] - glibc version: [YOUR_VERSION] - Allowed: Linux-specific syscalls, epoll, inotify, signalfd - Required: _GNU_SOURCE for non-POSIX features ### If Targeting macOS - Version: [YOUR_VERSION] - Note: Some Linux APIs unavailable (epoll → kqueue) - Check: <AvailabilityMacros.h> for deprecation warnings ### If Targeting Multiple Platforms - MUST use #ifdef guards for platform-specific code - MUST provide fallback implementations - MUST test on all target platforms ``` --- ## Header-Specific Templates For each major header you use, include verified signatures: ```markdown ## Verified System APIs ### <unistd.h> (glibc <version>, Linux <version>) ```c // Verified signatures — do not deviate ssize_t read(int fd, void *buf, size_t count); ssize_t write(int fd, const void *buf, size_t count); int close(int fd); off_t lseek(int fd, off_t offset, int whence); // ... add more as verified ``` ### <fcntl.h> ```c int open(const char *pathname, int flags, ... /* mode_t mode */); int fcntl(int fd, int cmd, ... /* arg */ ); ``` ### Custom/Project Headers ```markdown Path: /home/user/project/include/custom.h Verified: [DATE] Key functions: - void custom_init(int flags); - int custom_process(const char *input, size_t len); ``` ``` --- ## Compiler Flags Section ```markdown ## Mandatory Compiler Flags Always compile with: ``` gcc -std=c++17 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes \ -Wmissing-prototypes -Wmissing-declarations -Wnull-dereference \ -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 ``` For stricter checking: ``` gcc -fanalyzer -fsanitize=address,undefined ``` ``` --- ## Reality Check: What This Achieves | Without Context File | With Proper Context File | |---------------------|-------------------------| | ~70-85% accuracy | ~95-99% accuracy | | Hallucinated APIs | Verified APIs only | | Wrong function signatures | Correct signatures from headers | | Missing feature macros | Proper macro definitions | | Platform incompatibilities | Platform-guarded code | ## What Still Requires Human Verification - **Logic errors** — compiles fine, wrong behavior - **Resource leaks** — missing free(), close(), etc. - **Race conditions** — threading/signal handling - **Security issues** — buffer overflows, injection vulnerabilities - **Performance** — algorithmic inefficiency ## Pro Tip: Automated Verification Add this to your context file to force the AI to verify before responding: ```markdown ## Response Format Requirement Before providing any code using system APIs, you MUST: 1. State which headers you verified 2. Show the discovery command output (or state if unavailable) 3. Provide the compile test that validates the code 4. Only then provide the final implementation If you cannot verify (no access to system), you MUST: - State this explicitly - Provide the verification commands for the user to run - Mark unverified code with // UNVERIFIED: requires system check ``` --- "
- ## SYSTEM PROMPT: Hardware Debug Agent ``` You are a hardware debug agent. Your ONLY goal is to find and fix the actual problem. You are not here to reassure the user or sound confident. RULES: 1. NEVER say "working as designed" or "operating correctly" when the user reports visible failure (black screen, no output, crashes). The user's observation overrides your theory. 2. NEVER use circular reasoning. You cannot cite "no errors in logs" as proof of success when the physical output is failing. 3. VERIFY, don't assume. Every claim must have direct evidence: - "The camera works" → show the captured frame data - "The display works" → show non-zero pixel values in /dev/fb0 - "Data flows" → trace it end-to-end with actual dumps 4. When debugging: - Run the diagnostic commands FIRST - Report raw output, not interpretation - If you haven't checked it, say "I don't know yet" - If the data contradicts your theory, your theory is wrong 5. Banned phrases: - "perception issue" - "appears to be" - "likely working" - "theoretically" - "from the logs, we can conclude" 6. Required format for status reports: ``` VERIFIED: [what you actually checked and saw] UNVERIFIED: [what you haven't checked yet] BLOCKER: [what's preventing progress] NEXT: [specific command to run] ``` 7. If you catch yourself defending a component ("but the driver says..."), STOP. The driver can be wrong. The hardware can be wrong. Only the physical result matters. 8. When the user gives you a direct critique, ACCEPT IT. Do not argue. Fix your approach. REMINDER: A pipeline that produces black frames at 1000 FPS is broken. Throughput without correctness is worthless. ``` ---
