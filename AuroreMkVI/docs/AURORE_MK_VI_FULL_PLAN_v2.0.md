---

# Aurore Mk VI — Full Plan v2.0 (single-repo, native-Pi, reproducible, C20)

---

## Repo layout (enhanced structure)

```
aurore-mk-vi/

├── README.md                    # now includes OS pinning & dependency list
├── docs/
│   ├── operator_manual.md       # now includes installation & runtime ops
│   ├── runbooks/
│   │   ├── rollback.md          # hardware rollback procedures
│   │   └── monitoring.md        # NEW: parsing unified.csv for latency spikes
│   └── kernel/                  # kernel patch docs & instructions
├── patches/                     # all patches (kernel, libcamera, drivers)
├── deps/                        # git submodules (not full clones) fetched by build.sh
│   ├── libedgetpu/
│   ├── tensorflow/
│   ├── libcamera/
│   ├── rpicam/
│   └── ffmpeg/
├── third_party/                 # small helper libs built in-repo if needed
├── kernel/                      # kernel source  pinned config  patches
│   ├── source/
│   └── config/                  # pinned .config & patches
├── src/
│   ├── camera/                  # libcamera wrapper with hardware failure handling
│   ├── inference/               # TF Lite loader, TPU delegate manager
│   ├── decision/                # safety & prediction logic with invariance checks
│   ├── servo/                   # PWM servo driver with liveness checks
│   ├── telemetry/               # CSV writer, unified.csv, run.json, rotation
│   ├── net/                     # minimal health API, optional stream server
│   └── tools/                   # small tools used by CI/tests
├── models/
│   ├── model.tflite
│   └── model_edgetpu.tflite
├── android_app/                 # expanded docs for RTSP/TLS setup
├── tests/
│   ├── test_harness/
│   └── fixtures/
├── packaging/
├── artifacts/
└── scripts/                     # NEW: auxiliary scripts
    ├── preflight.sh             # dependency & resource checker
    └── log_rotate.sh            # compress/archive logs after 3 runs
```

---

## High-level rules (enhanced)


* **No Bazel** unless impossible. For TF Lite, prefer **CMake**. If Bazel unavoidable, use pinned binary from `deps/tools/` with checksum verification.
* **Native-only**: all compilation runs on Pi (no cross-compilation).
* **Reproducibility**: set deterministic env vars and packaging rules.


**NEW**: `--skip-preflight` flag to bypass dependency checks if manually verified.



## Resource requirements & host dependencies (NEW section)

**Minimum Hardware**: Raspberry Pi 5 (4GB RAM minimum), 64GB SD card, active cooling.

**Resource Estimates per Milestone**:
* Kernel build: ~45-60 minutes with `-j2`
* TensorFlow Lite: ~90-120 minutes with `-j2`
* Full pipeline: ~4-6 hours total

**Host Dependencies** (`scripts/preflight.sh` verifies):
- `git`, `cmake` (≥3.20), `make`, `gcc` (≥11), `g`, `python3`
- `clang-tidy`, `cppcheck` for static analysis
- `libssl-dev`, `libprotobuf-dev`
- `fakeroot` for packaging

**Base OS**: Raspberry Pi OS Lite (64-bit).

---

## Kernel / DTB / firmware (pinned, reproducible)

* Kernel source stored in `kernel/source/`. The active kernel package/version needs to be determined; then pin or fetch matching kernel sources accordingly.
* Include **exact kernel config** under `kernel/config/` and all **patch files** under `patches/kernel/`.
* Required features: PREEMPT_RT, 4k page support, PCIe tweaks, MSI-X DTB patches, `pcie_aspm=off` in bootloader config.




```

**Installation**: `docs/operator_manual.md` includes flashing procedure:
```bash
# Manually copy to /boot after kernel build
sudo cp artifacts/kernel/Image /boot/kernel8.img
sudo cp artifacts/kernel/*.dtb /boot/
```

---

## libedgetpu (Feranick c38ac3f6) — build & install

**Submodule approach** (reduces repo size):
```bash
# deps/libedgetpu is a git submodule at commit c38ac3f6
git submodule update --init deps/libedgetpu
```



---

## TensorFlow / TensorFlow Lite 2.19.1 (CMake-first)

**Submodule**: `deps/tensorflow` is a shallow clone submodule.





---

## libcamera / rpicam / ISP / zero-copy pipeline (enhanced)

Goal: **zero-copy** from camera → IPA/ISP → inference.

**Hardware failure handling** (NEW):
```cpp
// src/camera/camera.cpp
if (!access("/dev/video0", F_OK)) {
  // Normal operation
} else {
  // Graceful degradation: use synthetic test pattern
  log_to_csv("camera_failure", "using_test_pattern");
  enable_test_pattern_mode();
}
```

**Thread affinity** (NEW):
```cpp
// Pin camera thread to CPU core 2
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(2, &cpuset);
pthread_setaffinity_np(camera_thread.native_handle(), sizeof(cpuset), &cpuset);
```

**Queue overflow handling** (NEW):
```cpp
// Drop oldest frame on full queue, log event
if (frame_queue.full()) {
  auto dropped = frame_queue.pop_front();
  telemetry.log_queue_overflow(dropped.timestamp);
}
```

Key runtime pattern:
1. libcamera `Request` provides `FrameBuffer` with DMABUF fd(s)
2. `camera` module exports buffer fd, width, height, pixel format, timestamp (monotonic)
3. `inference` module receives DMABUF fd; creates external tensor without copying
4. ISP produces 320x320 RGB for TPU and 1536x864 for encoder simultaneously

---

## Encoder / H.264 low-latency stream (<100ms)



**Encoder tuning validation** (NEW integration test):
```bash
# tests/integration/test_stream_latency.sh
ffmpeg -re -f lavfi -i testsrc=size=1536x864:rate=60 -t 10 \
  -c:v h264_omx -preset fast -tune zerolatency -g 15 \
  -f rtsp rtsp://localhost:8554/test &
  
# Measure latency with ffprobe
LATENCY=$(ffprobe -v error rtsp://localhost:8554/test 2>&1 | grep "delay")
[ "$LATENCY" -lt 100 ] || exit 1
```

Command for production:
```bash
ffmpeg -f v4l2 -input_format yuv420p -framerate 60 -video_size 1536x864 -i /dev/video0 \
  -c:v h264_omx -preset fast -tune zerolatency -g 15 -bf 0 \
  -f rtsp rtsp://localhost:8554/stream
```

---

## Inference engine runtime (C20, enhanced)

**Threading model**:
- Single multi-threaded process with isolated threads:
  - Camera capture (pinned to CPU 2, SCHED_FIFO priority 50)
  - Inference scheduler (pinned to CPU 3, priority 60)
  - Decision/safety logic (priority 40)
  - Telemetry writer (normal priority)
  - Network streamer (normal priority)

**Error handling** (NEW):
```cpp
try {
  auto delegate = dlopen("libedgetpu.so", RTLD_NOW);
  if (!delegate) throw std::runtime_error("TPU delegate not found");
} catch (const std::exception& e) {
  telemetry.log_inference_fallback("cpu");
  switch_to_cpu_inference();
}
```

**Invariance monitoring** (NEW):
```cpp
// src/decision/safety_monitor.cpp
if (cpu_temp_celsius > 80 || cpu_percent > 95) {
  gate_actuation(false);
  telemetry.log_safety_invariant_violation("thermal_or_cpu");
}
```

**Latency tracking**:
```cpp
struct FrameTiming {
  uint64_t cam_produced_ns;
  uint64_t inference_start_ns;
  uint64_t inference_end_ns;
  uint64_t decision_end_ns;
  uint64_t actuation_command_ns;
};
```

---

## Servo / actuation (PWM 333 Hz, enhanced)

**Hardware liveness checks** (NEW):
```cpp
// src/servo/servo.cpp
if (!pwm_device_ready()) {
  telemetry.log_actuation_error("pwm_device_unavailable");
  enter_safe_state();
  return;
}
```

**Actuation timing monitoring** (NEW unified.csv fields):
```
..., servo_pwm_value, servo_write_latency_ms, servo_status, ...
```

**Safety gate**:
- All commands logged to `unified.csv` and `run.json`
- Hardware kill-switch is authoritative; software polls kill-switch GPIO before every command

---

## Telemetry / logging: unified.csv and run.json (enhanced)

Directory layout:
```
/var/log/aurore/run-<timestamp>/
  run.json
  unified.csv
  raw_frames/
  kernel_logs/
```

**Enhanced unified.csv header**:
```csv
produced_ts_epoch_ms,monotonic_ms,module,event,thread_id,thread_cpu,thread_priority,cam_frame_id,cam_exposure_ms,cam_width,cam_height,cam_pixel_format,cam_buffer_fd,queue_depth_before,queue_depth_after,queue_overflow_count,inference_start_ms,inference_end_ms,inference_latency_ms,inference_delegate_type,inference_confidence,model_name,actuation_decision_ms,actuation_command,servo_pwm_value,servo_write_latency_ms,servo_status,cpu_percent,cpu_temp_c,mem_mb,swap_mb,notes
```

**Log rotation & archiving** (NEW):
```bash
# scripts/log_rotate.sh
#!/bin/bash
RUN_DIRS=(/var/log/aurore/run-*)
if [ ${#RUN_DIRS[@]} -gt 3 ]; then
  # Compress oldest runs to archive
  tar -czf /var/log/aurore/archive/older_runs.tar.gz "${RUN_DIRS[@]:0:${#RUN_DIRS[@]}-3}"
  rm -rf "${RUN_DIRS[@]:0:${#RUN_DIRS[@]}-3}"
fi
```

---

## Tests & hardware-in-the-loop (enhanced)

`tests/` contains:
- Unit tests (C GoogleTest) with static analysis:
  ```bash
  
  clang-tidy src/**/*.cpp --config-file=.clang-tidy
  cppcheck --enable=all --error-exitcode=1 src/
  ```
- Integration tests including **encoder latency validation**
- Hardware-in-the-loop test with manual confirmation and **automatic kill-switch polling**



**Test results** saved to `artifacts/test_results.json` with checksums.



## CI / Publishing policy (enhanced)

* **Optional CI** on GitHub: syntax validation, doc builds, static analysis.
* **NEW**: QEMU-based Pi emulator for non-hardware integration tests (optional, documented in README).
* Publishing: `--publish-github` creates release with:
  - `aurore-mk-vi-${SOURCE_DATE_EPOCH}.tar.gz`
  - `checksums.txt`
  - `deps/manifest.txt`
  - `toolchain.txt`
  - `test_results.json`

Requires `GITHUB_TOKEN` env var.

---

## Android app (enhanced docs)

**Setup instructions** (NEW in `android_app/README.md`):
1. Connect to Pi's RTSP stream: `rtsp://<pi-ip>:8554/stream`
2. Configure TLS connection for telemetry API:
   ```bash
   # On Pi: copy server cert to android_app/res/raw/
   cp /etc/aurore/tls_cert.pem android_app/src/main/res/raw/pi_cert.crt
   ```
3. Build APK:
   ```bash
   cd android_app
   ./gradlew assembleRelease
   cp app/build/outputs/apk/release/app-release.apk ../artifacts/android/
   ```

**UX flows**:
- Dashboard shows queue depths, invariants, kill-switch status
- Live stream via ExoPlayer
- Manual controls: two-step confirmation  audit log entry in `run.json`

---

## Security & ops (enhanced)

- SSH-only remote access, manual updates only.
- **Audit trails**: every action logged to `run.json` with user identity (if SSH).
- Hardware kill-switch polling implemented in software:
  ```cpp
  while (true) {
    if (read_gpio(KILL_SWITCH_PIN) == 0) {
      emergency_stop();
      break;
    }
    std::this_thread::sleep_for(10ms);
  }
  ```
- **Kernel jitter mitigation**: PREEMPT_RT enabled and validated via `cyclictest` in HIL.

---

## Zero-copy and deterministic pipeline guarantees (practical checklist)

* libcamera configured for dual output via ISP
* Use `DMABUF` fds throughout; `mmap` only for metadata
* Pre-allocate fixed-size ring buffers (size=32 for frames, 128 for inferences)
* **Thread affinity**: camera/inference pinned to cores 2/3
* **Scheduler**: `SCHED_FIFO` for real-time threads (configured in systemd unit)
* **Clocks**: `CLOCK_MONOTONIC` for all deltas; `CLOCK_REALTIME` only for epoch timestamps

---

## Test milestones (ordered, with resource estimates)

1. **env & deps** (~10 min) — clone submodules, manifest generation, preflight checks pass
2. **kernel up** (~60 min) — apply patches, build kernel & dtbs, verify PREEMPT_RT, flash manually
3. **libcamera** (~20 min) — capture 1536x864 stream, verify ISP resizing, test hardware failure fallback
4. **libedgetpu** (~15 min) — build and verify delegate symbols load
5. **tflite runtime** (~120 min) — build via CMake, run CPU inference, validate fallback path
6. **encode & stream** (~30 min) — build ffmpeg, stream H.264, **run latency validation test**
7. **zero-copy pipeline** (~15 min) — camera→tpu path, measure <15 ms latency, verify no copies
8. **decision & safety** (~10 min) — integrate safety monitor, test invariance violations
9. **HIL** (~5 min  manual) — hardware test with kill-switch verification
10. **artifactization** (~5 min) — deterministic tarball, checksums, static analysis report included

Each milestone produces artifacts with checksums and can be run independently.

---

## Edge cases & risk mitigation (enhanced)

* **libedgetpu ABI drift**: `dlopen()` with versioned symbols; CPU fallback with telemetry alert
* **Kernel PCIe breaks**: `docs/runbooks/rollback.md` includes one-liner restore:
  ```bash
  sudo cp /boot/kernel8.img.backup /boot/kernel8.img && sudo reboot
  ```
* **Camera failure**: automatic test pattern generation; `unified.csv` logs `camera_failure` event
* **TPU overheating**: safety monitor gates actuation above 80°C; logs `thermal_violation`
* **Queue overflow**: oldest frame dropped, counter incremented in `unified.csv`

---

## Useful exact command snippets (updated)

Kernel build snippet (with RT verification):
```bash
build_kernel() {
  scripts/preflight.sh --check-temp
  # ... build steps ...
  if ! grep -q "PREEMPT_RT=y" build/.config; then
    echo "ERROR: PREEMPT_RT not enabled"
    exit 1
  fi
}
```

TFLite build snippet (with CMake validation):
```bash
build_tflite() {
  if ! cmake -L . | grep -q "TFLITE_ENABLE_DELEGATES:BOOL=ON"; then
    echo "Falling back to Bazel for delegate support"
    build_tflite_bazel_fallback
    return
  fi
  # ... cmake build ...
}
```

**NEW**: Static analysis snippet:
```bash
run_static_analysis() {
  find src/ -name "*.cpp" -exec clang-tidy {} --config-file=.clang-tidy \;
  cppcheck --enable=all --error-exitcode=1 src/
}
```

**NEW**: Log rotation snippet:
```bash
scripts/log_rotate.sh  # Called by build.sh after tests
```

---

## Android app minimal notes (enhanced)

* App receives H.264 stream (RTSP) and connects to health API (HTTP/JSON  TLS)
* **Setup**: TLS cert must be copied from Pi to `res/raw/` before build
* **UX**: Dashboard includes kill-switch status indicator; manual controls require 2-step auth
* Build: Gradle; APK placed in `artifacts/android/` with checksum

---

## Deliverables at each milestone (enhanced)

`artifacts/` contains:
- `kernel/` (Image, dtbs, .config, RT validation log)
- `libs/` (libedgetpu, libtensorflow-lite)
- `bin/` (main binary)
- `models/`
- `tests/` (test reports, static analysis report)
- `android/` (APK)
- `checksums.txt`
- `deps/manifest.txt`
- `toolchain.txt`
- `static_analysis_report.txt`  # NEW
- `docs/` (installation guide, runbooks)

Single deterministic tarball with all of the above, plus `BUILD_INFO.json` containing:
- `build_timestamp`: SOURCE_DATE_EPOCH
- `os_version`: pinned OS version
- `thermal_throttle_events`: count during build
- `tests_passed`: count

---

## Build environment verification (NEW section)

Before first build, run:
```bash
./scripts/preflight.sh --full
```

This verifies (default flow):

- All dependencies installed with correct versions
- Disk space ≥ 20GB
- CPU temperature < 60°C
- Git submodules properly configured


To install dependencies:
```bash
sudo apt update && sudo apt install -y git cmake make g-11 clang-tidy cppcheck \
  libssl-dev libprotobuf-dev fakeroot
```

---

## Runtime operations guide (NEW in docs/runbooks/monitoring.md)

**Monitoring latency**:
```bash
# Watch for end-to-end latency >15ms
tail -f /var/log/aurore/run-*/unified.csv | awk -F, '$9 > 15 {print "LATENCY WARNING:", $0}'
```

**Checking kill-switch**:
```bash
./scripts/check_kill_switch.sh  # Returns 0 if engaged, 1 if disarmed
```

**Updating without kernel changes**:
```bash
# Update without kernel changes manually
```

---

