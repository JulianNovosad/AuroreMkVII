# Aurore MkVII - Fire Control System

**Personal Hobby Project - Educational Use Only**

Real-time vision-based fire control system for Raspberry Pi 5. Designed for 120Hz frame processing with deterministic timing and safety monitoring.

---

## ⚠️ Disclaimer

This project is for **educational and skill acquisition purposes only**. The Raspberry Pi 5 platform is NOT suitable for safety-critical, military, or certification-bound applications. Do not deploy this system in any application where failure could cause injury, death, or property damage.

---

## 🔒 Security Requirements

**Minimum Dependency Versions** (to address known CVEs):

| Dependency | Minimum Version | CVE Addressed |
|------------|-----------------|---------------|
| OpenCV | >= 4.9.0 | CVE-2024-2167 (High), CVE-2024-3400 (Medium) |
| libwebp | >= 1.3.2 | CVE-2023-4863 (Critical) |
| glibc | >= 2.39-1ubuntu3 | CVE-2024-2961 (High) |
| Linux Kernel | >= 6.8.0-25 | CVE-2024-26581 (Medium) |

**Before building**, ensure your system is updated:

```bash
sudo apt update && sudo apt upgrade -y
```

See [docs/dependencies.md](docs/dependencies.md) for complete SBOM and vulnerability remediation details.

---

## System Overview

| Specification | Value |
|---------------|-------|
| Frame Rate | 120 Hz (8.333ms period) |
| Frame Period Tolerance | ±50μs |
| WCET Budget | ≤5.0ms |
| Jitter Budget | ≤5% at 99.9th percentile |
| Resolution | 1536×864 RAW10 |
| Safety Monitor | 1kHz (1ms period) |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Control Loop (120Hz)                         │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐         │
│  │   Vision    │───►│   Track     │───►│  Actuation  │         │
│  │  Pipeline   │    │   Compute   │    │   Output    │         │
│  │ FIFO=90     │    │ FIFO=85     │    │ FIFO=95     │         │
│  └─────────────┘    └─────────────┘    └─────────────┘         │
│         │                  │                  │                 │
│         └──────────────────┼──────────────────┘                 │
│                            │                                    │
│                     ┌──────▼──────┐                             │
│                     │   Safety    │                             │
│                     │   Monitor   │                             │
│                     │ FIFO=99     │                             │
│                     │   (1kHz)    │                             │
│                     └─────────────┘                             │
└─────────────────────────────────────────────────────────────────┘
```

### Thread Configuration

| Thread | Priority | CPU | Period | Phase Offset |
|--------|----------|-----|--------|--------------|
| safety_monitor | 99 | 3 | 1ms | 0ms |
| actuation_output | 95 | 2 | 8.333ms | 4ms |
| vision_pipeline | 90 | 2 | 8.333ms | 0ms |
| track_compute | 85 | 2 | 8.333ms | 2ms |

---

## Project Structure

```
AuroreMkVII/
├── CMakeLists.txt           # Build configuration
├── config/
│   └── config.json.example  # Configuration template
├── include/aurore/
│   ├── ring_buffer.hpp      # Lock-free SPSC ring buffer
│   ├── timing.hpp           # Real-time timing framework
│   ├── safety_monitor.hpp   # Safety monitor with software watchdog
│   └── camera_wrapper.hpp   # libcamera zero-copy wrapper
├── src/
│   ├── main.cpp             # Entry point
│   └── drivers/
│       └── camera_wrapper.cpp
├── tests/
│   ├── unit/
│   │   ├── ring_buffer_test.cpp
│   │   ├── timing_test.cpp
│   │   └── safety_monitor_test.cpp
│   └── timing/
│       └── wcet_measurement.cpp
└── scripts/
    ├── wcet_analysis.sh     # WCET analysis script
    └── jitter_monitor.sh    # Jitter monitoring script
```

---

## Build Instructions

### Prerequisites

#### For Native Build (laptop/desktop development):

**Step 1: Update system packages** (addresses CVE-2024-2961, CVE-2024-26581):
```bash
sudo apt update && sudo apt upgrade -y
```

**Step 2: Install build dependencies**:
```bash
# Ubuntu 24.04 / Debian 12
sudo apt install -y libopencv-dev libcamera-dev cmake g++ pkg-config libwebp7 libwebp-dev
```

**Step 3: Verify minimum versions** (security requirement):
```bash
# OpenCV >= 4.9.0
pkg-config --modversion opencv4

# libwebp >= 1.3.2
dpkg -l libwebp7 libwebp-dev | grep libwebp

# glibc >= 2.39-1ubuntu3
ldd --version

# Kernel >= 6.8.0-25
uname -r
```

If any version is below the minimum, run `sudo apt update && sudo apt upgrade` again or check Ubuntu security notices.

#### For Cross-Compilation (Raspberry Pi 5 target):
```bash
# Install ARM64 cross-compiler toolchain
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Verify installation
aarch64-linux-gnu-g++ --version
```

### Quick Start

#### Native Build (for development/testing on laptop):
```bash
./scripts/build-native.sh Release
cd build-native && ctest --output-on-failure
```

#### Cross-Compile for Raspberry Pi 5:
```bash
./scripts/build-rpi.sh Release
```

#### Deploy to Raspberry Pi 5:
```bash
# Set environment variables (optional, defaults shown)
export RPI_USER=pi
export RPI_HOST=aurorpi.local  # or IP address like 192.168.1.100

# Deploy binaries
./scripts/deploy-to-rpi.sh
```

### Manual Build

#### Native (x86_64):
```bash
mkdir build-native && cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### Cross-Compile for Raspberry Pi 5 (aarch64):
```bash
mkdir build-rpi && cd build-rpi
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-rpi5-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Verify architecture
file aurore
# Should show: ELF 64-bit LSB executable, ARM aarch64
```

### Run Tests

```bash
# Native tests (on laptop)
cd build-native
ctest --output-on-failure

# Target tests (deploy to RPi 5 first, then SSH in)
ssh pi@aurorpi.local
cd ~/aurore/build-rpi
ctest --output-on-failure
```

---

## Key Components

### 1. Lock-Free Ring Buffer (`ring_buffer.hpp`)

Single-Producer Single-Consumer (SPSC) lock-free ring buffer for zero-copy frame transfer:

```cpp
#include "aurore/ring_buffer.hpp"

aurore::LockFreeRingBuffer<ZeroCopyFrame, 4> buffer;

// Producer (vision pipeline)
ZeroCopyFrame frame = capture_frame();
buffer.push(frame);  // Returns false if full

// Consumer (tracking)
ZeroCopyFrame frame;
if (buffer.pop(frame)) {
    process_frame(frame);
}
```

**Features:**
- Cache-line aligned (prevents false sharing)
- Atomic operations with proper memory ordering
- Power-of-2 size for efficient modulo

### 2. Timing Framework (`timing.hpp`)

Real-time thread timing using `clock_nanosleep()` with `TIMER_ABSTIME`:

```cpp
#include "aurore/timing.hpp"

// 120Hz with 2ms phase offset
aurore::ThreadTiming timing(8333333, 2000000);

while (running) {
    bool on_time = timing.wait();
    if (!on_time) {
        // Handle deadline miss
    }
    process_frame();
}
```

**Features:**
- Absolute time sleep (prevents drift)
- Deadline miss detection
- Jitter measurement
- Phase offset support

### 3. Safety Monitor (`safety_monitor.hpp`)

1kHz safety monitoring with software watchdog:

```cpp
#include "aurore/safety_monitor.hpp"

aurore::SafetyMonitorConfig config;
config.vision_deadline_ns = 20000000;   // 20ms
config.actuation_deadline_ns = 2000000; // 2ms

aurore::SafetyMonitor monitor(config);
monitor.init();
monitor.start();

// In control loop threads:
monitor.update_vision_frame(sequence, timestamp_ns);
monitor.update_actuation_frame(sequence, timestamp_ns);

// In safety thread (1kHz):
if (!monitor.run_cycle()) {
    // Safety fault detected
}

// In main control loop (every 50ms):
monitor.kick_watchdog();  // Call directly to reset watchdog timer
// ... control loop work ...
```

**Features:**
- Vision/actuation deadline monitoring
- Frame stall detection
- Software watchdog with 50ms kick interval (60ms timeout)
- Callback-based fault reporting

### 4. Camera Wrapper (`camera_wrapper.hpp`)

libcamera wrapper with zero-copy OpenCV integration:

```cpp
#include "aurore/camera_wrapper.hpp"

aurore::CameraConfig config;
config.width = 1536;
config.height = 864;
config.fps = 120;

aurore::CameraWrapper camera(config);
camera.init();
camera.start();

ZeroCopyFrame frame;
if (camera->try_capture_frame(frame)) {
    cv::Mat img = camera->wrap_as_mat(frame);  // Zero-copy!
    cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
}
```

**Features:**
- DMA buffer mmap for zero-copy
- OpenCV Mat header wrapping
- Frame timestamp capture

### 5. Telemetry Writer (`telemetry_writer.hpp`)

Asynchronous telemetry logging for remote HUD rendering and session analysis:

```cpp
#include "aurore/telemetry_writer.hpp"

aurore::TelemetryWriter telemetry;
aurore::TelemetryConfig config;
config.log_dir = "logs";
config.enable_csv = true;
config.enable_json = true;

telemetry.start(config);

// In control loop (non-blocking):
telemetry.log_frame(detection_data, track_data, actuation_data, health_data);

// On shutdown:
telemetry.stop();

// Outputs: logs/run_<session>.csv and logs/run_<session>.json
```

**Features:**
- Async writer thread (non-blocking for control loops)
- CSV output (unified.csv format compatible with MkVI)
- JSON summary (run.json for quick status)
- Log rotation (configurable size/sessions)
- Backpressure handling with configurable drop policy

### 6. State Machine (`state_machine.hpp`)

FCS state machine implementing BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT transitions:

```cpp
#include "aurore/state_machine.hpp"

aurore::StateMachine fsm;

// Operator mode requests
fsm.request_freecam();
fsm.request_search();

// Per-frame updates
fsm.on_detection(detection);
fsm.on_tracker_update(track_solution);
fsm.on_gimbal_status(gimbal_status);
fsm.on_ballistics_solution(fire_control_solution);

// State query
if (fsm.state() == aurore::FcsState::TRACKING) {
    // Target locked
}

// Fault handling
fsm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
```

**Features:**
- 7-state FCS state machine per spec.md AM7-L1-MODE-002
- Guarded transitions with validation
- FAULT state latching (requires power cycle/reset)
- Operator authorization for ARMED state
- Interlock control for safety posture

### 7. Ballistic Solver (`ballistic_solver.hpp`)

Ballistic trajectory computation with precomputed lookup tables for p_hit estimation:

```cpp
#include "aurore/ballistic_solver.hpp"

aurore::BallisticSolver solver;
solver.initialize_lookup_table();  // Call once at startup

// Per-frame ballistic solution
auto solution = solver.solve(
    range_m,           // Target range in meters
    gimbal_el_deg,     // Gimbal elevation
    target_aspect,     // Target aspect angle
    muzzle_velocity    // From ammo profile
);

if (solution.has_value()) {
    float az_lead = solution->az_lead_deg;
    float el_lead = solution->el_lead_deg;
    float p_hit = solution->p_hit;
}
```

**Features:**
- Kinetic and drop engagement modes
- Precomputed p_hit lookup tables (PERF-005)
- Monte Carlo p_hit for legacy compatibility
- Configurable ammunition profiles
- Sub-1ms computation time

### 8. Fusion HAT+ Driver (`fusion_hat.hpp`)

I2C driver for SunFounder Fusion HAT+ gimbal controller with async command queuing:

```cpp
#include "aurore/fusion_hat.hpp"

aurore::FusionHat hat;
hat.init("/dev/i2c-1");  // Or hat.init_sim() for testing

// Authentication required before control
hat.authenticate(aurore::I2cAccessLevel::kGimbal);

// Gimbal control
auto status = hat.set_gimbal(azimuth_deg, elevation_deg);
if (status != aurore::I2cCommandStatus::kOk) {
    // Handle error
}

// Effector trigger (async, non-blocking)
hat.trigger_effector(pulse_ms);

// Safety interlock
hat.arm();
hat.disarm();
```

**Features:**
- I2C communication with GD32 MCU on Fusion HAT+
- 16-bit PWM resolution for gimbal servos
- Async command queuing (PERF-006)
- Access level authentication (SEC-007)
- Command sequence tracking for audit
- Simulated mode for testing without hardware

---

## Real-Time Configuration

### Kernel Boot Parameters

Edit `/boot/firmware/cmdline.txt`:

```
isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1
```

### CPU Affinity

```bash
# Move IRQs away from isolated CPUs
for irq in /proc/irq/*/smp_affinity_list; do
    echo "0-1" > "$irq" 2>/dev/null
done
```

### Disable Power Management

```bash
# Set CPU governor to performance
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" > "$cpu"
done
```

---

## Testing

### Unit Tests

```bash
cd build-native
ctest --output-on-failure
```

### WCET Measurement

```bash
# Run WCET analysis
./scripts/wcet_analysis.sh --samples=1000000

# Results in ./wcet_results/
```

### Jitter Monitoring

```bash
# Requires root for SCHED_FIFO
sudo ./scripts/jitter_monitor.sh --duration=60
```

---

## Requirements Traceability

This implementation addresses the following requirements from `spec.md`:

| Requirement | Implementation |
|-------------|----------------|
| AM7-L2-TIM-001 (120Hz) | `ThreadTiming` with 8333333ns period |
| AM7-L2-TIM-002 (WCET ≤5ms) | WCET measurement tool in `tests/timing/` |
| AM7-L2-TIM-003 (Jitter ≤5%) | Jitter monitoring script |
| AM7-L2-VIS-007 (Zero-copy) | `CameraWrapper::wrap_as_mat()` |
| AM7-L2-SAFE-001 (Fault inhibit) | `SafetyMonitor` with 1ms response |
| AM7-L3-TIM-001 (CLOCK_MONOTONIC_RAW) | `get_timestamp(ClockId::MonotonicRaw)` |
| AM7-L3-TIM-003 (SCHED_FIFO) | `configure_rt_thread()` in main.cpp |

---

## Next Steps (TODO)

The following subsystems are implemented and functional:

**Implemented:**
- `LockFreeRingBuffer`, `ThreadTiming`, `DeadlineMonitor`, `SafetyMonitor`, `CameraWrapper`
- `TelemetryWriter` - async telemetry logging with CSV/JSON output
- `StateMachine` - 7-state FCS state machine (BOOT→IDLE_SAFE→FREECAM→SEARCH→TRACKING→ARMED→FAULT)
- `BallisticSolver` - ballistic trajectory computation with lookup tables
- `FusionHat` - I2C driver for Fusion HAT+ gimbal controller
- `KcfTracker` - KCF visual tracker (1-2ms execution time)
- `OrbDetector` - ORB feature-based target detection

**Remaining TODO:**
- [ ] Complete vision pipeline integration (image preprocessor, color segmentation)
- [ ] Integrate tracker with state machine for automatic lock acquisition
- [ ] Complete HUD telemetry UNIX domain socket output
- [ ] Add integration tests with hardware
- [ ] Operator control interface (remote commands via UNIX socket)

---

## Cross-Compilation Workflow

### Development Workflow

The recommended workflow is to develop and test on your laptop, then cross-compile for deployment:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Develop on    │────►│  Cross-compile  │────►│  Deploy & Test  │
│    Laptop       │     │   for aarch64   │     │   on RPi 5      │
│  (x86_64)       │     │                 │     │                 │
│                 │     │                 │     │                 │
│ - Fast iteration│     │ - Verify ARM    │     │ - Real hardware │
│ - Unit tests    │     │   build works   │     │ - Integration   │
│ - Debug builds  │     │ - Static analysis│    │ - Performance   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### What You Can Test on Laptop

| Component | Test on Laptop | Notes |
|-----------|---------------|-------|
| Ring buffers | ✅ Yes | Architecture-independent |
| State machine logic | ✅ Yes | Pure C++ logic |
| Safety monitor | ✅ Yes | Logic tests work |
| Timing framework | ⚠️ Partially | Jitter differs on non-RT |
| Camera driver | ❌ No | Requires libcamera hardware |
| Gimbal control | ❌ No | Requires I2C hardware |
| WCET measurement | ❌ No | Must run on target |

### Raspberry Pi 5 Real-Time Configuration

For real-time operation on the target hardware:

1. **Install PREEMPT_RT kernel** (if not already):
   ```bash
   sudo apt install raspberrypi-kernel-rt
   ```

2. **Configure kernel boot parameters** (`/boot/firmware/cmdline.txt`):
   ```
   isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1
   ```

3. **Disable CPU frequency scaling**:
   ```bash
   for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
       echo "performance" | sudo tee "$cpu"
   done
   ```

4. **Run with root privileges** (required for SCHED_FIFO):
   ```bash
   sudo ./aurore
   ```

---

## License

This project is for personal/educational use. No warranty expressed or implied.

---

## References

- [libcamera documentation](https://libcamera.org/api-html/)
- [OpenCV documentation](https://docs.opencv.org/)
- [Linux PREEMPT_RT documentation](https://wiki.linuxfoundation.org/realtime/documentation/start)
- [clock_nanosleep(2) man page](https://man7.org/linux/man-pages/man2/clock_nanosleep.2.html)
