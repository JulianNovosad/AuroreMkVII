# Aurore Mk VII — Integrated Defense Engineering Requirements Package

**Document Classification:** PERSONAL DEVELOPMENT / EDUCATIONAL USE
**Document Version:** 5.0 (Hardware & Architecture Revision)
**Date:** 2026-03-03
**Status:** PERSONAL DEVELOPMENT VERSION — NOT FOR SAFETY-CRITICAL USE

**Disclaimer:** This document is for educational and skill acquisition purposes only. The Raspberry Pi 5 platform is NOT suitable for safety-critical, military, or certification-bound applications. Do not deploy this system in any application where failure could cause injury, death, or property damage.

---

## 1. Document Authority and Scope

This document is the single authoritative engineering control artifact for the Aurore Mk VII Man-portable Picatinny-mounted Fire Control System (FCS) — Personal Development Edition. It consolidates system requirements, decomposed subsystem requirements, interface control requirements, requirements traceability, and verification planning.

**Note:** This is a personal development project. No regulatory compliance (ITAR, MIL-STD, ISO) is claimed or required.

**External Standards Referenced (as design guidance only):**
- MIL-STD-810H — Environmental Engineering Considerations (design target, not certified)
- MIL-STD-461G — Electromagnetic Interference Characteristics (design target, not certified)
- MIL-STD-1913 — Picatinny Rail Specification
- ISO 13849-1/2 — Safety of Machinery (design inspiration, not certification)
- IEC 61508 — Functional Safety (design inspiration, not certification)
- IEC 60529 — Ingress Protection (design target)
- NIST FIPS 186-4 — ECDSA Digital Signature Standard
- NIST FIPS 198-1 — HMAC Authentication

---

## 2. Glossary and Definitions

All terms shall be interpreted according to the following quantitative definitions:

| Term | Definition |
|------|------------|
| **Deterministic** | Timing variation ≤ 5% at 99.9th percentile over 10M consecutive cycles |
| **Frame Period** | 8.333ms nominal at 120Hz; tolerance ±50μs |
| **Critical Path** | Execution threads: vision_pipeline, track_compute, actuation_output |
| **Classical Algorithm** | Non-learning algorithm with deterministic execution path; limited to: convolution, morphological operations, thresholding, connected components, Hough transform, template matching, feature detection (ORB, SIFT), optical flow (Lucas-Kanade), CSRT tracking. Bounded RANSAC permitted (max 100 iterations). Neural network inference prohibited. |
| **CSRT** | Channel Spatial Reliability Tracker — discriminative correlation filter-based tracker with spatial reliability masks for robust object tracking |
| **Zero-Copy** | No memcpy(), memmove(), or buffer copy operations between camera input and track output; DMA transfers permitted |
| **Fail-Safe** | System transitions to safe state on fault detection; safe state = interlock inhibit, gimbal hold, fire disabled |
| **Stale (Range Data)** | Timestamp age > 100ms from acquisition timestamp to processing timestamp |
| **Invalid (Range Data)** | Checksum mismatch, NaN, infinity, or value outside operational range [0.5m, 5000m] |
| **Immediately** | Within one control cycle (≤ 1ms) |
| **Precomputed** | Computed at build time or boot time; not computed during operational cycle |
| **Immutable** | Stored in .rodata section; no runtime writes permitted |
| **Real-Time Scheduling Policy** | SCHED_FIFO with priority 80-99 on Linux kernel (PREEMPT_RT optional) |
| **Asynchronous** | Operation completes without blocking caller; maximum latency 10ms |
| **Non-Blocking** | Operation returns within 10μs worst-case regardless of buffer or system state |
| **Monotonic (Timestamp)** | Each timestamp ≥ previous timestamp; no backward time steps permitted |
| **Authoritative Clock** | CLOCK_MONOTONIC_RAW (clock_id=4) from Linux kernel |
| **Statically Analyzable** | Passes static analysis (Coverity, clang-tidy) with zero high-severity findings |
| **WCET** | Worst-Case Execution Time determined by measurement-based statistical analysis (100M+ samples under stress) |
| **Jitter** | Standard deviation of period measurement; specified at 99.9th percentile |
| **Diagnostic Coverage (DC)** | DC = detected dangerous faults / total dangerous faults × 100%; target ≥ 90% |
| **MTTFd** | Mean Time To Dangerous Failure; target ≥ 876,000 hours (100 years) per channel (design goal, not certified) |
| **Common Cause Failure (CCF)** | Score per ISO 13849-2 Annex D; target ≥ 65 points (design goal, not certified) |
| **Hardware Fault Tolerance (HFT)** | HFT = N means N+1 faults can cause loss of safety function; target HFT = 1 |
| **PFH** | Probability of dangerous Failure per Hour; target < 10⁻⁷/hour for design target |
| **Safe State** | Interlock = inhibit (servo command), gimbal = hold position, fire authorization = disabled |
| **Frame** | Single image acquisition and processing cycle at 120Hz rate |
| **Control Cycle** | 1ms nominal control loop iteration for safety monitoring |
| **Brownout** | Supply voltage drops below minimum operating threshold (4.75V for 5V rail) |
| **Tamper-Evident** | Physical seal that shows visible evidence of unauthorized access |
| **libcamera** | Open-source camera support library for Linux; provides low-level RAW10 frame acquisition from MIPI CSI-2 sensors |
| **OpenCV** | Open Source Computer Vision library; provides image processing algorithms (convolution, thresholding, tracking) |
| **VideoCore VII** | Raspberry Pi 5 GPU; provides hardware acceleration for image processing (color space conversion, scaling, convolution) |
| **ARM NEON** | ARM SIMD instruction set; provides parallel processing for matrix math and image processing |
| **I2C** | Inter-Integrated Circuit bus; 2-wire serial communication protocol for peripheral devices |
| **libgpiod** | Linux userspace library for GPIO line access via character device interface |
| **CMake** | Cross-platform build system generator; authoritative build toolchain for Aurore Mk VII |
| **Fusion HAT+** | SunFounder Fusion HAT+; I2C-connected expansion board with GD32 MCU, 12-channel PWM (16-bit), 4-channel ADC (12-bit), motor drivers, audio I/O, and battery power management |

---

## 3. Security Architecture

### 3.1 Security Requirements Overview

The system shall implement defense-in-depth security architecture addressing authentication, cryptography, and audit logging.

### 3.2 Level 1 Security Requirements

**AM7-L1-SEC-001**: System shall implement cryptographic authentication on all external interfaces.

**AM7-L1-SEC-002**: System shall implement secure boot with cryptographic signature verification (optional, requires custom bootloader).

**AM7-L1-SEC-003**: System shall implement tamper-evident audit logging with cryptographic integrity.

**AM7-L1-SEC-004**: System shall implement role-based access control for configuration and maintenance operations.

**AM7-L1-SEC-005**: System shall support secure field firmware updates with rollback protection.

### 3.3 Level 2 Security Requirements

**AM7-L2-SEC-001**: All inter-component messages shall be authenticated using HMAC-SHA256 with 256-bit keys.

**AM7-L2-SEC-002**: All firmware images shall be signed using ECDSA P-256 per FIPS 186-4.

**AM7-L2-SEC-003**: Audit log shall record: security events, configuration changes, fault events, firmware updates with cryptographic chain-of-custody.

**AM7-L2-SEC-004**: System shall implement replay attack prevention using monotonically increasing sequence numbers on all authenticated messages.

**AM7-L2-SEC-005**: System shall implement session timeout of 300 seconds for maintenance interfaces.

**AM7-L2-SEC-006**: Cryptographic keys shall be stored in protected storage (TPM, secure element, or encrypted file).

### 3.4 Level 3 Security Requirements

**AM7-L3-SEC-001**: HMAC-SHA256 shall be computed over: message payload + sequence number + timestamp. Verification shall occur before message processing.

**AM7-L3-SEC-002**: Secure boot shall verify ECDSA signature on bootloader, kernel, and application before execution. Boot shall halt on signature verification failure.

**AM7-L3-SEC-003**: Audit log entry format shall be: `{timestamp: u64, event_id: u16, severity: u8, data: u8[], hmac: u32[8]}`. Each entry shall be individually signed.

**AM7-L3-SEC-004**: Sequence numbers shall be uint32_t with wrap-aware comparison (RFC 1982). Sequence gap > 1000 shall trigger security fault.

**AM7-L3-SEC-005**: Firmware update shall verify: (a) ECDSA signature, (b) version number > current version, (c) dual-bank flash integrity before switching.

**AM7-L3-SEC-006**: Keys shall be generated using hardware RNG (or /dev/urandom) with ≥ 256 bits entropy. Keys shall never be stored in plaintext.

---

## 4. Requirement Taxonomy

Requirements are structured hierarchically:

* **Level 1 (L1)** — Operational, system, and design intent with quantitative acceptance criteria
* **Level 2 (L2)** — Subsystem, interface, and behavioral constraints with thresholds and tolerances
* **Level 3 (L3)** — Implementation-bounding requirements with measurement methodology

All L2 requirements trace to exactly one L1 parent. All L3 requirements trace to exactly one L2 parent. All requirements are testable, auditable, and version-controlled with verification methods defined in Section 11.

---

## 5. Level 1 System and Compliance Requirements (L1)

### 5.1 System Identity

**AM7-L1-SYS-001**: System mass shall be ≤ 2.5 kg including battery. System dimensions shall fit within 200mm × 150mm × 100mm envelope. System shall mount to MIL-STD-1913 Picatinny rail per specification.

**AM7-L1-SYS-002**: System enclosure shall achieve IP67 ingress protection per IEC 60529 (design target). System shall not require user service. Tamper-evident seals shall be applied to all enclosure fasteners.

**AM7-L1-SYS-003**: System timing shall be bounded with: (a) WCET ≤ 5.0ms per AM7-L2-TIM-002, (b) jitter ≤ 5% at 99.9th percentile per AM7-L2-TIM-003, (c) all execution loops shall have statically bounded iteration counts.

**AM7-L1-SYS-004**: System shall not modify executable code at runtime. System shall not employ neural networks, genetic algorithms, or reinforcement learning. Static lookup tables initialized at boot are permitted.

### 5.2 Design Targets

**AM7-L1-COM-001**: System shall pass MIL-STD-810H Methods 501.7 Procedure I, 502.7 Procedure I, 516.8 Procedure I (design target, not certified).

**AM7-L1-COM-002**: System shall pass MIL-STD-461G requirements CE101, CE102, RE102, CS114, RS103 (design target, not certified).

**AM7-L1-COM-003**: System safety architecture shall target ISO 13849 Performance Level e (PL e), Category 3 (design inspiration, not certified).

### 5.3 Environmental Operating Conditions

**AM7-L1-ENV-001**: System shall operate at ambient temperatures from -20°C to +55°C (desktop verification target; -40°C design goal requires additional testing).

**AM7-L1-ENV-002**: System shall operate at relative humidity 5% to 95% non-condensing.

**AM7-L1-ENV-003**: System shall operate at altitude from sea level to 4500m.

### 5.4 Physical Target Definitions

**AM7-L1-TGT-CLASS-001**: System shall detect and track two defined physical target classes for Verification and Validation (V&V) testing.

**AM7-L2-TGT-CLASS-001**: **Calibration/Debug Target** shall be defined as: (a) 50×50 mm flat sheet, (b) bifacial color scheme (Red: RAL 3000 / Yellow: RAL 1003), (c) matte surface finish, (d) used for contrast calibration, color threshold validation, and static detection testing.

**AM7-L2-TGT-CLASS-002**: **Primary Kinetic Target** shall be defined as a sub-scale 1:X miniature helicopter with: (a) dimensions 80mm L × 30mm W × 30mm H, (b) fuselage: satin/matte charcoal grey (RAL 7016 equivalent), (c) canopy: translucent navy blue, (d) rotor assemblies: matte grey non-reflective, (e) used for dynamic tracking validation, template matching, and motion prediction testing.

**AM7-L3-TGT-CLASS-001**: Physical target specifications shall be used for: (a) contrast-ratio requirement definition, (b) template-matching algorithm validation, (c) detection probability testing per AM7-L2-VIS-004, (d) CSRT tracker performance validation.

**AM7-L3-TGT-CLASS-002**: Target signatures shall be documented with: (a) spectral reflectance data, (b) contrast ratios against typical backgrounds (sky, foliage, urban), (c) radar cross-section (if applicable), (d) thermal signature (if applicable).

---

## 6. Level 2 Subsystem and Interface Requirements (L2)

### 6.1 Timing and Scheduling

**AM7-L2-TIM-001**: System shall sustain processing rate of 120 Hz ±1% for minimum 30 minutes at ambient temperature 25°C ±10°C with CPU load ≤80%.

**AM7-L2-TIM-002**: End-to-end latency from sensor trigger to actuator command shall have WCET ≤ 5.0ms measured from interrupt handler entry to GPIO toggle, verified by measurement-based statistical WCET analysis.

**AM7-L2-TIM-003**: Runtime jitter shall not exceed 5% of nominal period (417μs at 120Hz) measured at 99.9th percentile over minimum 10M consecutive cycles.

**AM7-L2-TIM-004**: System shall achieve CPU utilization ≤ 70% average, ≤ 85% peak at 120Hz processing rate.

### 6.2 Vision Subsystem

**AM7-L2-VIS-001**: Vision algorithms shall be limited to: convolution, morphological operations, thresholding, connected components, Hough transform, template matching, ORB/SIFT feature detection, Lucas-Kanade optical flow, CSRT tracking. Bounded RANSAC permitted (max 100 iterations). Neural network inference is prohibited.

**AM7-L2-VIS-002**: Image acquisition shall be synchronized to system clock source CLOCK_MONOTONIC_RAW with synchronization accuracy ≤ 100μs.

**AM7-L2-VIS-003**: Vision pipeline shall process 1536×864 RAW10 images at 120Hz with latency ≤ 3.0ms from frame start to track output.

**AM7-L2-VIS-004**: Target detection shall achieve probability of detection Pd ≥ 95% at false alarm rate FAR ≤ 10⁻⁴ per frame for specified target signatures.

**AM7-L2-VIS-005**: System shall use libcamera for low-level RAW10 frame acquisition from MIPI CSI-2 camera interface.

**AM7-L2-VIS-006**: System shall use OpenCV for image processing operations (convolution, thresholding, morphological operations, CSRT tracking).

**AM7-L2-VIS-007**: Integration between libcamera and OpenCV shall be zero-copy: OpenCV cv::Mat headers shall reference libcamera DMA buffers without intermediate memory copies.

**AM7-L2-VIS-008**: System shall implement CSRT (Channel Spatial Reliability Tracker) for continuous target tracking at 120Hz loop frequency at 1536×864 resolution.

**AM7-L2-VIS-009**: CSRT tracker shall utilize hardware acceleration (ARM NEON SIMD and/or VideoCore VII GPU) to meet timing requirements.

### 6.3 Actuation Subsystem

**AM7-L2-ACT-001**: Gimbal command updates shall be issued at 120 Hz ±1% synchronized to frame boundary with tolerance ±50μs.

**AM7-L2-ACT-002**: Gimbal motion shall be constrained to: elevation -10° to +45°, azimuth ±90°, velocity ≤ 60°/s, acceleration ≤ 120°/s².

**AM7-L2-ACT-003**: Actuation command latency shall be ≤ 2.0ms from compute output to gimbal servo command.

### 6.4 Safety Subsystem

**AM7-L2-SAFE-001**: Any detected fault shall force inhibit within one frame period (≤ 8.33ms at 120Hz) measured from fault detection interrupt to GPIO transition.

**AM7-L2-SAFE-002**: System shall implement dual-channel safety architecture with software comparator and GPIO monitoring. Single fault shall not cause loss of safety function (HFT = 1, design goal).

**AM7-L2-SAFE-003**: System shall achieve diagnostic coverage DCavg ≥ 90% for all safety-related faults, verified by fault tree analysis.

**AM7-L2-SAFE-004**: System shall achieve MTTFd ≥ 100 years (≥ 876,000 hours) per safety channel (design goal, component reliability dependent).

**AM7-L2-SAFE-005**: System shall achieve common cause failure score CCF ≥ 65 points (design goal per ISO 13849-2 Annex D).

**AM7-L2-SAFE-006**: System shall achieve PFH < 10⁻⁷/hour for safety functions (design goal).

**AM7-L2-SAFE-007**: Safety function self-test shall execute at power-on and every 100ms during operation. Self-test shall verify: comparator function, interlock GPIO, watchdog timer.

### 6.5 Ballistic Engine

**AM7-L2-BALL-001**: System shall implement a ballistic engine for aim point calculation based on configurable ammunition profiles.

**AM7-L2-BALL-002**: Ballistic profiles shall be stored in config.json with parameters: muzzle velocity (m/s), ballistic coefficient, sight height (mm), zero range (m).

**AM7-L2-BALL-003**: Ballistic solution shall calculate aim point offset based on: target range, target velocity, ammunition profile, environmental conditions (if available).

**AM7-L2-BALL-004**: Ballistic calculation shall complete within 1ms to meet 120Hz loop requirement.

### 6.6 Remote HUD Interface

**AM7-L2-HUD-001**: System shall provide telemetry data for remote HUD rendering on Operator Control Interface (laptop).

**AM7-L2-HUD-002**: HUD telemetry shall include: (a) central reticle with dynamic lead indication (ballistic solution), (b) range readout, (c) active state machine status, (d) target bounding box (CSRT tracker output).

**AM7-L2-HUD-003**: HUD rendering shall be performed on Remote Operator Interface (laptop), not on Pi 5, to preserve compute cycles for real-time processing.

**AM7-L2-HUD-004**: HUD telemetry update rate shall be 120 Hz synchronized to frame boundary.

---

## 7. Level 3 Decomposed Requirements (L3)

### 7.1 Timing and Determinism

**AM7-L3-TIM-001**: System shall use CLOCK_MONOTONIC_RAW (clock_id=4) as sole time source for all timing-critical operations. Verification: binary analysis confirming no calls to gettimeofday(), time(), rdtsc, or other time sources.

**AM7-L3-TIM-002**: No calls to malloc(), calloc(), realloc(), or new shall occur after system initialization complete signal. Verification: static analysis with allocation tracking + runtime instrumentation with LD_PRELOAD hooks over 10M cycles.

**AM7-L3-TIM-003**: All threads in critical path (vision_pipeline, track_compute, actuation_output) shall execute under SCHED_FIFO policy with priorities: vision_pipeline=90, track_compute=85, actuation_output=95. Verification: runtime policy inspection via sched_getscheduler().

**AM7-L3-TIM-004**: All loops shall have statically bounded maximum iteration counts. Verification: static analysis confirming no unbounded loops.

**AM7-L3-TIM-005**: System shall lock all memory pages (mlockall) to prevent page faults during operation. Verification: /proc/self/status VmLck verification.

### 7.2 Vision Pipeline

**AM7-L3-VIS-001**: Vision processing shall use zero-copy buffers defined as: no memcpy() or buffer copy operations between camera input and track output. DMA transfers permitted. Verification: memory access instrumentation confirming zero intermediate copies.

**AM7-L3-VIS-002**: All vision code shall pass static analysis with: no undefined behavior, no buffer overflows, no null pointer dereferences. Tools: Coverity or clang-tidy. Verification: zero high-severity findings.

**AM7-L3-VIS-003**: Image buffer alignment shall be 64-byte aligned for SIMD optimization (NEON). Verification: runtime alignment check.

**AM7-L3-VIS-004**: Vision pipeline shall implement watchdog timer with 10ms timeout. Timeout shall trigger safety fault. Verification: fault injection test.

### 7.3 Actuation Subsystem

**AM7-L3-ACT-001**: Gimbal motion constraints (elevation: -10° to +45°, azimuth: ±90°, velocity: ≤ 60°/s, acceleration: ≤ 120°/s²) shall be stored in read-only data section (.rodata). Verification: memory map inspection confirming .rodata placement.

**AM7-L3-ACT-002**: Gimbal command sequence numbers shall be monotonically increasing uint32_t. Sequence gap detection shall trigger hold command. Verification: sequence injection test.

**AM7-L3-ACT-003**: Gimbal servo shall implement position, velocity, and torque limits. Limit violation shall trigger fault. Verification: limit injection test.

### 7.4 Safety Subsystem

**AM7-L3-SAFE-001**: Interlock servo shall default to inhibit (GPIO low) on: (a) main power loss, (b) 5V rail brownout < 4.75V, (c) CPU reset, (d) safety channel fault. Default state shall be achieved via hardware pull-down resistor (10kΩ) independent of software. Verification: power-loss test with oscilloscope confirmation.

**AM7-L3-SAFE-002**: Range data shall be revoked if: (a) age > 100ms from timestamp to processing, (b) checksum mismatch, (c) value out of range [0.5m, 5000m], (d) NaN or infinity. Revocation shall occur within one control cycle (≤ 1ms). Verification: fault injection test.

**AM7-L3-SAFE-003**: Safety channels A and B shall execute diverse software implementations. Channel A: C implementation. Channel B: Rust implementation (no unsafe blocks). Verification: code review confirming independent implementations.

**AM7-L3-SAFE-004**: Safety comparator shall detect channel mismatch within 100μs. Mismatch shall trigger inhibit. Verification: mismatch injection test with oscilloscope.

**AM7-L3-SAFE-005**: Watchdog timer shall require periodic kick every 50ms ±10ms. Missed kick shall trigger system reset and inhibit. Note: Watchdog timeout (60ms max) exceeds frame-period inhibit budget (8.33ms); fast faults (camera timeout, range data) are detected by software within 8.33ms. Watchdog is for catastrophic CPU hangs only. Verification: watchdog timeout test.

**AM7-L3-SAFE-006**: Fault register shall be latched and non-clearable except via power cycle. Verification: fault latch test.

### 7.5 Memory Protection

**AM7-L3-MEM-001**: System shall implement memory configuration with: .text: read-only, execute; .rodata: read-only, no-execute; .data: read-write, no-execute; .bss: read-write, no-execute; stack: read-write, no-execute. Verification: /proc/self/maps inspection.

**AM7-L3-MEM-002**: Stack overflow protection shall be implemented with: stack canaries (GCC -fstack-protector-strong); guard region at stack boundary. Verification: stack overflow injection test.

**AM7-L3-MEM-003**: Buffer overflow protection shall be implemented with: FORTIFY_SOURCE=2; ASLR enabled. Verification: buffer overflow injection test.

### 7.6 Secure Field Update

**AM7-L3-SEC-001**: Firmware updates shall be cryptographically signed with ECDSA P-256. Signature verification shall occur before update application. Verification: update with invalid signature shall be rejected.

**AM7-L3-SEC-002**: Firmware updates shall implement rollback protection. Version number shall monotonically increase. Downgrade attempts shall be rejected. Verification: downgrade attempt test.

**AM7-L3-SEC-003**: Firmware update shall implement dual-bank flash with fail-safe rollback on boot failure. Verification: power-loss during update test.

### 7.7 Build System

**AM7-L3-BUILD-001**: System shall use CMake as the authoritative build system generator. Verification: CMakeLists.txt present, cmake --build executes successfully.

**AM7-L3-BUILD-002**: System shall target C++17 standard (ISO/IEC 14882:2017). Verification: CMake CXX_STANDARD set to 17, compiler flags -std=c++17.

**AM7-L3-BUILD-003**: System shall be compiled with GCC 12+ or clang 15+ for ARM64 (aarch64-linux-gnu). Verification: compiler version check.

**AM7-L3-BUILD-004**: Build configuration shall include: -O3 optimization, -march=native (or -mcpu=cortex-a72), -mfpu=neon for NEON SIMD enablement. Verification: compiler flags audit.

### 7.8 Computational Resource Management

**AM7-L3-OPT-001**: System shall utilize ARM NEON SIMD instructions for matrix math and image processing operations. Verification: binary analysis (objdump) confirms NEON instruction usage (VMLA, VLD, VST).

**AM7-L3-OPT-002**: System shall explore and document VideoCore VII GPU utilization for: (a) color space conversion (RAW10 → RGB/BGR), (b) image resizing/scaling, (c) convolution operations (Gaussian blur, edge detection). Verification: vcgencmd or ARM Performance Monitoring Unit (PMU) counters.

**AM7-L3-OPT-003**: GPU offload shall not violate determinism requirements (AM7-L1-SYS-003). Verification: jitter measurement with GPU acceleration enabled vs. disabled; Δ ≤ 5%.

**AM7-L3-OPT-004**: Performance comparison shall document: (a) CPU-only pipeline latency, (b) GPU-accelerated pipeline latency, (c) NEON-optimized vs. scalar performance. Verification: benchmark report with 100M+ samples.

---

## 8. Interface Control Documents

### 8.1 ICD-001: Camera → Compute Interface

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | MIPI CSI-2 (4-lane) via Raspberry Pi 5 camera interface |
| **Sensor** | Raspberry Pi Camera Module 3 (Sony IMX708) or equivalent |
| **Data Format** | RAW10, 1536×864 resolution at 120fps (native sensor resolution) |
| **Pixel Clock** | 120 MHz ±100 ppm |
| **Timestamp Format** | uint64_t, nanoseconds since CLOCK_MONOTONIC_RAW epoch |
| **Timestamp Accuracy** | ≤ 100μs accuracy relative to frame start |
| **Authentication** | Frame hash (SHA256) computed asynchronously; HMAC-SHA256 over header + hash. Hash computed within one frame period (8.33ms) after frame capture complete. HMAC verification occurs before frame is passed to vision pipeline. HMAC failure triggers fault AM7-FLT-AUTH-CAM and frame is dropped. |
| **Sequence Number** | uint32_t monotonically increasing, wrap-aware comparison (RFC 1982) |
| **Frame Timeout** | > 20ms triggers fault AM7-FLT-CAM-TIMEOUT |
| **Backpressure** | Ring buffer, 4 frames depth, oldest dropped on overflow |
| **Physical Layer** | Raspberry Pi 5 camera connector (15-pin FPC) |
| **Cable Length** | Maximum 2 meters (official camera cable or equivalent) |

**Message Structure:**
```
Frame Header:
  - sync_word:      u32 (0xAURORE01)
  - frame_id:       u32 (monotonic)
  - timestamp_ns:   u64 (CLOCK_MONOTONIC_RAW nanoseconds)
  - exposure_us:    u32 (microseconds)
  - gain:           u16 (ISO × 10)
  - reserved:       u16
  - frame_hash:     u32[8] (SHA256 of pixel data, computed asynchronously)
  - hmac:           u32[8] (HMAC-SHA256 over header + frame_hash)

Pixel Data:
  - RAW10 packed:   1536 × 864 × 10 bits = 1,658,880 bytes
```

### 8.2 ICD-002: Compute → Gimbal Interface (PWM Servo via Fusion HAT+)

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | I2C to GD32 MCU (SunFounder Fusion HAT+) |
| **I2C Address** | 0x17 (23 decimal) |
| **Bus Speed** | 100 kHz (standard mode) or 400 kHz (fast mode) |
| **Command Format** | PWM duty cycle for servo channels (elevation + azimuth) |
| **Units** | Pulse width: 1000µs to 2000µs (5% to 10% duty cycle at 50Hz) |
| **Position Range** | Elevation: -10° to +45° (mapped to 1000-2000µs); Azimuth: ±90° (mapped to 1000-2000µs) |
| **Velocity** | ≤ 60°/s (command rate-limited in software) |
| **Update Rate** | 120 Hz ±1% (once per frame period) |
| **Timing Tolerance** | ±50μs from frame boundary |
| **Authentication** | Command logged with HMAC; I2C write verified via ACK |
| **Command Timeout** | No command > 20ms triggers gimbal hold (software fallback) |
| **Sequence Gap** | Gap > 10 triggers fault |
| **Physical Layer** | I2C via GPIO2 (SDA) / GPIO3 (SCL) on Raspberry Pi 40-pin header |
| **PWM Channels** | Channel 0: Elevation servo; Channel 1: Azimuth servo |
| **PWM Frequency** | 50 Hz nominal (20,000µs period) |
| **PWM Resolution** | 16-bit (0-65535); 1000µs ≈ 3277, 2000µs ≈ 6554 at 50Hz |
| **Controller** | GD32 MCU (ARM Cortex-M) onboard Fusion HAT+ |

**Command Structure:**
```
Gimbal Command (software structure before PWM conversion):
  - sync_word:      u32 (0xAURORE02)
  - sequence:       u32 (monotonic)
  - elevation_deg:  float (-10.0 to +45.0 degrees)
  - azimuth_deg:    float (-90.0 to +90.0 degrees)
  - velocity_dps:   float (degrees/second, max 60)
  - flags:          u16 (bit 0: immediate, bit 1: relative)
  - hmac:           u32[8] (HMAC-SHA256 truncated)

PWM Conversion (executed by FusionHAT::setGimbal()):
  - elevation_pulse_us = map(elevation_deg, -10, +45, 1000, 2000)
  - azimuth_pulse_us = map(azimuth_deg, -90, +90, 1000, 2000)
  - Write PWM via I2C to GD32 MCU (address 0x17)
```

**C++ Library Call (Python API reference):**
```cpp
// Python reference (SunFounder API):
// from fusion_hat.pwm import PWM
// pwm = PWM(0, freq=50)
// pwm.pulse_width_percent(50)  // 50% = 1500µs = 90° center

// C++ equivalent (via I2C):
FusionHAT hat(0x17);  // I2C address
hat.setGimbal(elevation_deg, azimuth_deg);  // Converts to PWM, writes via I2C
```

**Failure Behavior:**
- I2C NACK → retry 3 times, then fault + gimbal hold
- I2C timeout (> 10ms) → trigger safety fault
- PWM signal loss → servos hold position (servo default behavior)

### 8.3 ICD-003: Compute → Interlock Interface (I2C Servo Command via Fusion HAT+)

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | I2C (Inter-Integrated Circuit) bus to GD32 MCU (SunFounder Fusion HAT+) |
| **Bus Speed** | 100 kHz (standard mode) or 400 kHz (fast mode) |
| **I2C Address** | 0x17 (23 decimal) |
| **Signal Type** | Servo PWM command via Fusion HAT+ PWM channel 2 |
| **Fail-Safe Default** | Servo default position on I2C loss (servo hardware default) |
| **Response Time** | ≤ 1ms I2C write to servo command execution |
| **Interlock Encoding** | Specific pulse width = INHIBIT/ENABLE state (1000µs = inhibit, 2000µs = enable) |
| **Authentication** | Interlock state signed in audit log |
| **Connector** | Fusion HAT+ PWM channel 2 (P2) via 2.54mm 3-pin servo connector |
| **Pull-up Resistors** | 10KΩ onboard on SDA and SCL (Fusion HAT+) |

**Interlock States:**
```
Servo Command 1000µs pulse width:  INHIBIT — Fire disabled (default, fail-safe)
Servo Command 2000µs pulse width:  ENABLE  — Fire authorized
Servo Command 1500µs pulse width:  NEUTRAL — 90° center position (zeroing)

Transition INHIBIT→ENABLE: Requires valid fire authorization from safety system
Transition ENABLE→INHIBIT: Immediate on any fault, power loss, or safety revocation
```

**PWM Command Sequence (via Fusion HAT+ GD32 MCU):**
1. I2C start condition
2. Write I2C address 0x17
3. Write PWM channel register (channel 2)
4. Write pulse width value (1000µs for INHIBIT, 2000µs for ENABLE)
5. I2C stop condition
6. Verify ACK

**C++ Library Call:**
```cpp
FusionHAT hat(0x17);
hat.setInterlock(true);   // 2000µs = ENABLE
hat.setInterlock(false);  // 1000µs = INHIBIT
```

---

### 8.4 ICD-004: Compute → Logger Interface

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | Ring buffer in shared memory |
| **Buffer Size** | 64KB circular buffer |
| **Entry Format** | {timestamp: u64, event_id: u16, severity: u8, flags: u8, data_len: u8, data: u8[], hmac: u32[8]} |
| **Overflow Behavior** | Drop oldest entries (circular overwrite) |
| **Write Latency** | Non-blocking, ≤ 10μs worst-case |
| **Entry Integrity** | HMAC-SHA256 per log entry |
| **Retrieval Interface** | USB-C debug interface or network (SSH) |
| **Capacity** | Minimum 100,000 entries before overwrite |
| **Retention** | Persistent storage (SSD or eMMC boot recommended) |

**Log Entry Structure:**
```
Log Entry:
  - timestamp_ns:   u64 (CLOCK_MONOTONIC_RAW nanoseconds)
  - event_id:       u16 (event type identifier)
  - severity:       u8 (0=debug, 1=info, 2=warning, 3=error, 4=critical)
  - flags:          u8 (bit 0: security, bit 1: safety, bit 2: fault)
  - data_len:       u8 (0 to 200 bytes)
  - sequence:       u32 (global monotonic sequence)
  - data:           u8[data_len] (variable payload)
  - hmac:           u32[8] (HMAC-SHA256 truncated)
```

**Event IDs:**
- 0x0001: System boot
- 0x0002: Firmware update start
- 0x0003: Firmware update complete
- 0x0010: Safety fault detected
- 0x0011: Inhibit engaged
- 0x0012: Inhibit released
- 0x0020: Authentication failure
- 0x0021: Sequence gap detected
- 0x0030: Camera timeout
- 0x0031: Gimbal command timeout
- 0x0040: Watchdog timeout
- 0x0050: Temperature warning
- 0x0051: Temperature critical

### 8.5 ICD-005: Operator Control Application Interface

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | UNIX domain socket (SOCK_SEQPACKET) or shared memory ring buffer |
| **Socket Path** | `/run/aurore/operator_control.sock` (permissions: 0660, owner: aurore:operator) |
| **Ring Buffer Size** | 4KB circular buffer (512 messages × 8 bytes per message) |
| **Message Format** | See below |
| **Data Format** | Little-endian; 64-bit alignment |
| **Timestamp Format** | uint64_t, nanoseconds since CLOCK_MONOTONIC_RAW epoch |
| **Sequence Number** | uint32_t monotonically increasing, wrap-aware comparison (RFC 1982) |
| **Authentication** | HMAC-SHA256 over message header + payload |
| **Message Timeout** | > 100ms triggers input discard |
| **Rate Limiting** | Maximum 120 messages/second; excess dropped |
| **Backpressure** | Ring buffer full → drop newest, set overflow flag |

**Operator Input Message Structure:**
```
Operator Input Message (64 bytes total):
  - sync_word:      u32 (0xAURORE05)
  - message_id:     u16 (command type identifier)
  - sequence:       u32 (monotonic, wrap-aware)
  - timestamp_ns:   u64 (CLOCK_MONOTONIC_RAW nanoseconds)
  - payload:        u8[32] (command-specific data)
  - hmac:           u32[8] (HMAC-SHA256 truncated)
```

**Message IDs (Operator → System):**
- 0x0101: MODE_REQUEST — Request state transition
- 0x0102: GIMBAL_COMMAND — Manual look direction (rate-based)
- 0x0103: ZOOM_COMMAND — Zoom in/out request
- 0x0104: TARGET_SELECT — Manual target selection (coordinates)
- 0x0105: TARGET_CONFIRM — Confirm automatic target
- 0x0106: TARGET_REJECT — Reject automatic target
- 0x0107: ARM_REQUEST — Request ARMED state entry
- 0x0108: DISARM_REQUEST — Request ARMED state exit
- 0x0109: EMERGENCY_INHIBIT — Immediate FAULT transition
- 0x010A: HEARTBEAT — Operator presence indicator (required every 500ms)

**Message Payload Formats:**

```
MODE_REQUEST (0x0101):
  - target_mode:    u8 (0=IDLE/SAFE, 1=FREECAM, 2=SEARCH, 3=TRACKING, 4=ARMED)
  - reserved:       u8[3]

GIMBAL_COMMAND (0x0102):
  - azimuth_rate:   i16 (milliradians/second × 100, range: ±1067 = ±60°/s)
  - elevation_rate: i16 (milliradians/second × 100, range: ±1067 = ±60°/s)

ZOOM_COMMAND (0x0103):
  - zoom_direction: i8 (-1=out, 0=stop, +1=in)
  - zoom_rate:      u8 (0-10, max 10% FOV/second)
  - reserved:       u16

TARGET_SELECT (0x0104):
  - cursor_x:       u16 (pixel coordinates, 0-1536)
  - cursor_y:       u16 (pixel coordinates, 0-864)
  - confidence:     u8 (operator confidence 0-100)
  - reserved:       u8[2]

TARGET_CONFIRM (0x0105):
  - target_id:      u32 (automatic target identifier)

TARGET_REJECT (0x0106):
  - target_id:      u32 (automatic target identifier)
  - reason:         u8 (0-255 reason code)
  - reserved:       u8[3]

ARM_REQUEST (0x0107):
  - authorization:  u32 (two-key sequence: 0xAURORE_ARM)
  - reserved:       u32

DISARM_REQUEST (0x0108):
  - authorization:  u32 (0xAURORE_DISARM)
  - reserved:       u32

EMERGENCY_INHIBIT (0x0109):
  - magic:          u32 (0xAURORE_ESTOP — no authentication required)
  - reserved:       u32

HEARTBEAT (0x010A):
  - operator_id:    u32 (authenticated operator identifier)
  - sequence:       u32 (heartbeat counter)
```

**System Response Message Structure:**
```
System Response Message (64 bytes total):
  - sync_word:      u32 (0xAURORE06)
  - message_id:     u16 (response type identifier)
  - sequence:       u32 (matches request sequence)
  - timestamp_ns:   u64 (CLOCK_MONOTONIC_RAW nanoseconds)
  - status:         u8 (0=ACK, 1=NACK, 2=PENDING)
  - error_code:     u8 (NACK reason code)
  - payload:        u8[28] (response-specific data)
  - hmac:           u32[8] (HMAC-SHA256 truncated)
```

**Response Message IDs (System → Operator):**
- 0x0201: MODE_ACK — State transition acknowledged
- 0x0202: MODE_NACK — State transition rejected
- 0x0203: SYSTEM_STATE — Current state machine status
- 0x0204: TARGET_STATUS — Current target lock status
- 0x0205: FAULT_STATUS — Current fault register

**System State Message Format:**
```
SYSTEM_STATE (0x0203):
  - current_mode:   u8 (0=BOOT, 1=IDLE/SAFE, 2=FREECAM, 3=SEARCH, 4=TRACKING, 5=ARMED, 6=FAULT)
  - interlock:      u8 (0=inhibit, 1=enable)
  - target_lock:    u8 (0=no lock, 1=locked)
  - fault_active:   u8 (0=clear, 1=active)
  - reserved:       u8[4]
```

**Authentication:**
- All messages (except EMERGENCY_INHIBIT) shall be authenticated with HMAC-SHA256
- HMAC key derived from operator authentication at session start
- HMAC computed over: sync_word + message_id + sequence + timestamp + payload
- Verification failure → message discarded, security event logged

**Rate Limiting:**
- Maximum 120 input messages/second accepted
- HEARTBEAT required every 500ms; missing heartbeat → transition to IDLE/SAFE
- GIMBAL_COMMAND and ZOOM_COMMAND sampled at 120 Hz; excess dropped

**Failure Behavior:**
- Socket disconnect → transition to IDLE/SAFE within 100ms
- Invalid HMAC → message discarded, security fault logged
- Sequence gap > 100 → session re-authentication required
- Buffer overflow → newest message dropped, warning logged

### 8.6 ICD-006: HUD Telemetry Interface

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | UNIX domain socket (SOCK_SEQPACKET) or shared memory ring buffer |
| **Socket Path** | `/run/aurore/hud_telemetry.sock` (permissions: 0660, owner: aurore:operator) |
| **Ring Buffer Size** | 8KB circular buffer (1024 messages × 8 bytes per message) |
| **Message Format** | See below |
| **Data Format** | Little-endian; 64-bit alignment |
| **Timestamp Format** | uint64_t, nanoseconds since CLOCK_MONOTONIC_RAW epoch |
| **Update Rate** | 120 Hz synchronized to frame boundary |
| **Authentication** | HMAC-SHA256 over message header + payload |

**HUD Telemetry Message Structure (64 bytes total):**
```
HUD Telemetry Message:
  - sync_word:      u32 (0xAURORE07)
  - message_id:     u16 (telemetry type identifier)
  - sequence:       u32 (monotonic, wrap-aware)
  - timestamp_ns:   u64 (CLOCK_MONOTONIC_RAW nanoseconds)
  - payload:        u8[32] (telemetry data)
  - hmac:           u32[8] (HMAC-SHA256 truncated)
```

**Telemetry Message IDs (System → HUD):**
- 0x0301: RETICLE_DATA — Central reticle with lead indication
- 0x0302: TARGET_BOX — CSRT bounding box coordinates
- 0x0303: BALLISTIC_SOLUTION — Aim point offset
- 0x0304: SYSTEM_STATUS — State machine status, range, health

**Message Payload Formats:**

```
RETICLE_DATA (0x0301):
  - reticle_x:      i16 (screen coordinate, -32768 to +32767)
  - reticle_y:      i16 (screen coordinate)
  - lead_offset_x:  i16 (ballistic lead, milliradians × 100)
  - lead_offset_y:  i16 (ballistic lead, milliradians × 100)

TARGET_BOX (0x0302):
  - box_x:          u16 (top-left X, pixels)
  - box_y:          u16 (top-left Y, pixels)
  - box_width:      u16 (pixels, 0-1536)
  - box_height:     u16 (pixels, 0-864)
  - confidence:     u8 (0-100)
  - reserved:       u8[3]

BALLISTIC_SOLUTION (0x0303):
  - elevation_adj:  i16 (milliradians × 100, positive = up)
  - azimuth_adj:    i16 (milliradians × 100, positive = right)
  - range_m:        u16 (meters, 0-5000)
  - ammo_id:        u8 (profile index from config.json)
  - reserved:       u8

SYSTEM_STATUS (0x0304):
  - state_machine:  u8 (0=BOOT, 1=IDLE/SAFE, 2=FREECAM, 3=SEARCH, 4=TRACKING, 5=ARMED, 6=FAULT)
  - interlock:      u8 (0=inhibit, 1=enable)
  - target_lock:    u8 (0=no lock, 1=locked)
  - fault_active:   u8 (0=clear, 1=active)
  - cpu_temp_c:     u16 (CPU temperature × 10, 0-1000 = 0°C to 100°C)
  - reserved:       u8[2]
```

**Failure Behavior:**
- Socket disconnect → HUD displays "NO DATA" warning
- Invalid HMAC → message discarded, security event logged
- Stale telemetry (> 100ms) → HUD displays warning indicator

### 8.7 ICD-007: SunFounder Fusion HAT+ Interface

| Parameter | Specification |
|-----------|---------------|
| **Protocol** | I2C (Inter-Integrated Circuit) bus to GD32 MCU |
| **Bus Speed** | 100 kHz (standard mode) or 400 kHz (fast mode) |
| **I2C Address** | 0x17 (23 decimal) |
| **MCU** | GD32 (ARM Cortex-M based) onboard Fusion HAT+ |
| **Library** | Native C++17 library derived from SunFounder Python reference implementation |
| **Driver Strategy** | I2C communication with GD32 MCU via /dev/i2c-* |
| **PWM Channels** | 12 channels (P0-P11), 16-bit resolution (0-65535) |
| **PWM Frequency** | 1 Hz to 65,535 Hz (50 Hz nominal for servo control) |
| **Pulse Width Range** | 1000µs to 2000µs (standard servo signal) |
| **Channel Allocation** | Channel 0: Elevation servo; Channel 1: Azimuth servo; Channel 2: Interlock servo |
| **ADC Channels** | 4 channels, 12-bit resolution |
| **I2C Connectors** | P2.54 4-pin + SH1.0 4-pin (QWIIC/STEMMA QT compatible) |
| **Pull-up Resistors** | 10KΩ onboard on SDA and SCL |

**I2C Communication Protocol:**
```
I2C Transaction Format:
  - Start condition
  - I2C address 0x17 + Write bit
  - Register address (PWM channel, command type)
  - Data (PWM value, frequency, etc.)
  - Stop condition
```

**C++ Library Interface (Python API reference):**
```cpp
// Python reference (SunFounder API):
// from fusion_hat.pwm import PWM
// pwm = PWM(0, freq=50)
// pwm.pulse_width_percent(50)

// C++ equivalent interface:
class FusionHAT {
public:
    FusionHAT(uint8_t addr = 0x17, int i2c_bus = 1);
    
    // PWM control (channel 0-11)
    void setPWM(uint8_t channel, uint16_t pulse_width);  // pulse_width: 0-65535
    void setServoPWM(uint8_t channel, float pulse_width_us);  // pulse_width_us: 1000-2000
    
    // High-level functions
    void setInterlock(bool enabled);  // Channel 2: 2000µs = enable, 1000µs = inhibit
    void setGimbal(float elevation_deg, float azimuth_deg);  // Channels 0, 1
    
    // ADC read (channels 0-3)
    uint16_t readADC(uint8_t channel);
    
    // Utility
    uint8_t getAddress() const;
    bool isConnected() const;  // I2C ping test
    
private:
    int m_i2c_fd;
    uint8_t m_addr;  // 0x17
};
```

**Servo Zeroing Function:**
- Physical button on Fusion HAT+ (double-press)
- Sets all PWM channels to 1500µs (90° center position)
- Used for mechanical servo calibration during assembly

**Failure Behavior:**
- I2C NACK → retry 3 times, then fault
- I2C timeout (> 10ms) → trigger safety fault
- Invalid channel → return error, log warning
- GD32 MCU unresponsive → system fault, gimbal hold

---

## 9. Safety Architecture

### 9.1 Unified Safety Architecture (C++17)

```
                    ┌─────────────────────────────────────┐
    Sensors ───────►│     Primary Safety Channel          │
                    │     (C++17 with software            │
                    │      self-monitoring)               │
                    │                                     │
                    │  ┌──────────────┐  ┌──────────────┐ │
                    │  │ Main Compute │  │ Self-Monitor │ │
                    │  │   Thread     │  │   Thread     │ │
                    │  └──────┬───────┘  └──────┬───────┘ │
                    │         │                 │         │
                    │         └───────┬─────────┘         │
                    │                 ▼                   │
                    │         ┌───────────────┐           │
                    │         │  Comparator   │           │
                    │         │  (Software)   │           │
                    │         └───────┬───────┘           │
                    └─────────────────┼───────────────────┘
                                      │
                                      ▼
                              ┌───────────────┐
                              │ I2C Interlock │
                              │ Servo Command │
                              └───────────────┘
```

**Note:** This is a learning architecture. Raspberry Pi 5 is NOT certified for safety-critical applications. The unified C++17 architecture replaces the previous dual-channel C/Rust design for implementation simplicity while maintaining safety through software self-monitoring and I2C interlock commands.

### 9.2 Safety Metrics (Design Goals)

| Metric | Target | Verification Method |
|--------|--------|---------------------|
| Performance Level | PL e (target) | Architecture review |
| Category | Category 3 (target) | Architecture review |
| HFT (Hardware Fault Tolerance) | 1 | FMEA analysis |
| DCavg (Diagnostic Coverage) | ≥ 90% | Fault tree analysis |
| MTTFd (per channel) | ≥ 100 years | Component reliability analysis (target) |
| CCF (Common Cause Failure) | ≥ 65 points | ISO 13849-2 Annex D checklist (target) |
| PFH (Probability of Failure per Hour) | < 10⁻⁷/hour | Reliability block diagram (target) |

### 9.3 Safety Function Requirements

**AM7-L3-SAFE-010**: Safety system shall be implemented in unified C++17 with software self-monitoring thread. Verification: code review, thread architecture audit.

**AM7-L3-SAFE-011**: Self-monitoring thread shall independently verify main compute thread outputs via: (a) sequence number validation, (b) range checking, (c) checksum verification. Verification: fault injection test.

**AM7-L3-SAFE-012**: Safety comparator shall be implemented in software with I2C interlock output. Target detection ≤ 100μs. Verification: timing analysis.

**AM7-L3-SAFE-013**: Interlock output shall be via I2C servo command per ICD-003. Single fault shall result in inhibit. Verification: single fault injection test.

**AM7-L3-SAFE-014**: Safety system shall implement periodic self-test every 100ms testing: comparator function, I2C interlock drive, watchdog timer. Verification: self-test coverage analysis.

### 9.4 Fault Detection and Response

| Fault Type | Detection Method | Response Time | Action |
|------------|------------------|---------------|--------|
| Power loss | Hardware brownout detector | ≤ 1ms | Servo default position |
| CPU reset | Watchdog timeout | ≤ 100ms | Servo default position |
| I2C failure | I2C NACK/timeout | ≤ 1ms | Fault + inhibit |
| Camera timeout | Frame timestamp monitor | ≤ 20ms | Inhibit |
| Gimbal timeout | Command sequence monitor | ≤ 20ms | Hold |
| Range data stale | Timestamp age check | ≤ 1ms | Revoke fire auth |
| Range data invalid | Checksum/range check | ≤ 1ms | Revoke fire auth |
| Authentication failure | HMAC verification | ≤ 1ms | Inhibit |
| Sequence gap | Sequence number monitor | ≤ 1ms | Inhibit |
| Temperature critical | Thermal sensor | ≤ 10ms | Inhibit |
| Memory fault | MPU/ASLR violation | ≤ 1μs | Reset + Inhibit |
| Stack overflow | Stack canary | ≤ 1μs | Reset + Inhibit |

---

## 10. Operating Modes, Control Interfaces, and Remote Operation

### 10.1 Operating System Selection

**AM7-L1-OS-001**: System shall run Raspberry Pi OS Lite (64-bit) with Linux kernel configured for PREEMPT_RT (best-effort real-time).

**AM7-L1-OS-002**: System shall disable all unnecessary services, interfaces, and daemons at boot to minimize attack surface, power consumption, and timing jitter.

**AM7-L2-OS-001**: System shall explicitly reject and not install: (a) desktop environments (GNOME, KDE, XFCE, LXDE), (b) X11 or Wayland display servers, (c) browser applications or JavaScript runtimes, (d) Linux Mint or other non-Raspberry-Pi-OS distributions.

**AM7-L2-OS-002**: System shall disable HDMI output and Bluetooth radio at boot via config.txt and cmdline.txt configuration. Wi-Fi shall be enabled for development only. Ethernet shall be the sole operational control interface during system operation.

**AM7-L2-OS-003**: System shall disable the following systemd services at boot: bluetooth.service, ModemManager.service, wpa_supplicant.service (if Ethernet only), avahi-daemon.service, triggerhappy.service, pigpio.service, nodered.service, apt-daily.service, apt-daily-upgrade.service, phd54875.service (random wait), systemd-networkd-wait-online.service.

**AM7-L2-OS-004**: System shall achieve boot time from power-on to application ready of ≤ 15 seconds.

**AM7-L3-OS-001**: Verification of OS configuration shall be performed via: (a) boot log inspection (/var/log/boot.log, dmesg), (b) systemctl list-units --state=running confirmation, (c) config.txt and cmdline.txt audit.

**AM7-L3-OS-002**: Power consumption and latency impact of service disablement shall be documented with before/after measurements: idle current draw, boot time, interrupt latency distribution.

**AM7-L3-OS-003**: Network interface enumeration shall confirm eth0 is bound to control application and wlan0 is disabled or unconfigured during operational use. Verification: ip link show, ethtool eth0.

**AM7-L3-OS-004**: Service disablement shall persist across reboot. Verification: systemctl is-enabled <service> returns "disabled" or "masked" for all services in AM7-L2-OS-003.

---

### 10.2 Control Architecture (Native Application)

**AM7-L1-CTL-001**: Operator control application shall be written in C++ using the same toolchain (GCC/G++) as the main real-time control system.

**AM7-L1-CTL-002**: Operator control application shall run as a separate process from the real-time control loop, with no shared execution context or blocking dependencies.

**AM7-L2-CTL-001**: Inter-process communication between operator control application and real-time system shall use: (a) UNIX domain sockets (SOCK_SEQPACKET), or (b) shared memory ring buffer with atomic sequence counters.

**AM7-L2-CTL-002**: Operator control application shall never block, preempt, or delay real-time threads under any operational condition including: input buffer overflow, network congestion, or application crash.

**AM7-L2-CTL-003**: System shall explicitly prohibit: (a) web servers (nginx, Apache, lighttpd), (b) WebSockets, (c) HTTP/HTTPS protocols, (d) JavaScript runtimes (Node.js, Deno, Bun), (e) Electron or Chromium Embedded Framework, (f) Python-based UI frameworks for operational control.

**AM7-L2-CTL-004**: System shall prohibit remote desktop technologies (VNC, RDP, X11 forwarding) during operational use. Remote desktop is permitted for development and debugging only.

**AM7-L3-CTL-001**: IPC latency shall be ≤ 1ms at 99.9th percentile measured from operator input to real-time system receipt. Verification: timestamp delta measurement over 1M samples.

**AM7-L3-CTL-002**: Binary analysis shall confirm no web dependencies. Verification: ldd analysis confirms no linkage to libcurl, libnghttp, libwebsocket, node, v8; strings analysis confirms no HTTP/WebSocket URLs in binary.

**AM7-L3-CTL-003**: Thread isolation shall be verified via /proc/[pid]/task inspection confirming: (a) real-time threads have SCHED_FIFO policy, (b) operator control threads have SCHED_OTHER policy, (c) no thread affinity conflicts.

---

### 10.3 Remote Operator Interface

**AM7-L1-IF-001**: Remote operator control shall be performed via direct Ethernet cable connection between laptop and system. Network topology shall be point-to-point with no intermediate switches or routers during operational use.

**AM7-L1-IF-002**: All operator inputs shall be rate-limited and bounded. No operator command may directly drive actuators without state machine validation.

**AM7-L2-IF-001**: Operator input sampling rate shall be ≥ 120 Hz. Verification: input event timestamp analysis over 30-second interval.

**AM7-L2-IF-002**: Operator input latency (laptop input to system receipt) shall be ≤ 10 ms at 99th percentile. This is a non-real-time path requirement; real-time control loop is unaffected.

**AM7-L2-IF-003**: Mouse drag input shall map to camera/gimbal look direction. Horizontal mouse movement → azimuth command. Vertical mouse movement → elevation command. Mapping shall be rate-based (velocity control), not position-based.

**AM7-L2-IF-004**: Scroll wheel input shall map to zoom in/out command. Zoom shall be digital (region-of-interest crop) or optical (if equipped with motorized lens). Zoom rate shall be bounded to ≤ 10% FOV per second.

**AM7-L2-IF-005**: Keyboard keybinds shall be provided for: (a) mode switching (FREECAM, SEARCH, TRACKING), (b) target selection (confirm, reject), (c) arm/disarm authorization, (d) emergency inhibit (immediate FAULT transition).

**AM7-L2-IF-006**: No operator command shall directly drive actuators. All operator inputs shall be translated to high-level commands (look direction, mode selection, authorization state) and processed through the state machine per Section 10.4.

**AM7-L3-IF-001**: Loss of Ethernet link shall trigger immediate transition to IDLE/SAFE state. Verification: cable disconnect test with mode transition logged within 100ms.

**AM7-L3-IF-002**: Invalid operator input (out-of-range values, malformed messages, unknown commands) shall be ignored and logged with severity=warning. Verification: invalid input injection test.

**AM7-L3-IF-003**: Input buffer overflow shall drop newest incoming messages and assert a warning flag. Verification: buffer saturation test with high-rate input injection.

**AM7-L3-IF-004**: All operator inputs shall be filtered through the state machine. State machine shall reject commands invalid for current state. Verification: state-command matrix coverage test.

**AM7-L3-IF-005**: Ethernet link status shall be monitored at ≥ 10 Hz via ethtool or netlink socket. Link down shall be detected within 100ms. Verification: link status polling test.

---

### 10.4 Operating Modes (State Machine)

**AM7-L1-MODE-001**: System shall implement a formal state machine with explicit, enumerated state transitions only. Implicit transitions, spontaneous transitions, and undocumented transitions are prohibited.

**AM7-L1-MODE-002**: System shall implement exactly seven (7) mandatory operating states: BOOT, IDLE/SAFE, FREECAM, SEARCH, TRACKING, ARMED, FAULT.

**AM7-L2-MODE-001**: **BOOT** state: System shall enter BOOT state immediately after power-on. BOOT state shall perform: hardware initialization, memory lock, thread creation, service binding, self-test. BOOT state shall transition to IDLE/SAFE on successful initialization or FAULT on failure.

**AM7-L2-MODE-002**: **IDLE/SAFE** state: System shall enter IDLE/SAFE state after successful BOOT. IDLE/SAFE state shall maintain: interlock inhibit (GPIO low), gimbal hold position, no target lock, no fire authorization. IDLE/SAFE shall transition to FREECAM on operator request or FAULT on fault detection.

**AM7-L2-MODE-003**: **FREECAM** state: System shall enter FREECAM state on operator request from IDLE/SAFE. FREECAM state shall allow: manual gimbal control via operator input, camera streaming, no automatic target lock. FREECAM shall prohibit: fire authorization, automatic tracking. FREECAM shall transition to IDLE/SAFE, SEARCH, or FAULT.

**AM7-L2-MODE-004**: **SEARCH** state: System shall enter SEARCH state on operator request from FREECAM or IDLE/SAFE. SEARCH state shall perform: automatic FOV scanning, target detection per AM7-L2-VIS-004, first valid target acquisition. SEARCH shall transition to TRACKING on valid lock, IDLE/SAFE on operator cancel, or FAULT on fault.

**AM7-L2-MODE-005**: **TRACKING** state: System shall enter TRACKING state on successful target lock from SEARCH. TRACKING state shall maintain: continuous target lock, gimbal servo active, confidence monitoring, stability monitoring. TRACKING shall transition to ARMED (if lock valid and stable), SEARCH (on target loss), IDLE/SAFE (on operator cancel), or FAULT (on fault).

**AM7-L2-MODE-006**: **ARMED** state: System shall enter ARMED state only from TRACKING state. ARMED state shall require: (a) valid target lock, (b) timing stability (jitter ≤ 5% at 99.9th percentile), (c) zero active faults, (d) explicit operator authorization. ARMED state shall enable: fire authorization (interlock may transition to ENABLE). ARMED shall transition to IDLE/SAFE (on disarm), TRACKING (on target loss), or FAULT (on fault).

**AM7-L2-MODE-007**: **FAULT** state: System shall enter FAULT state from any state on any fault detection per Section 9.4. FAULT state shall force: interlock inhibit (GPIO low), gimbal hold, fire authorization disabled, fault logging. FAULT shall transition to BOOT (on power cycle) or IDLE/SAFE (on fault clear and manual reset).

**AM7-L3-MODE-001**: State transition table shall be explicitly defined as follows:

| Source     | BOOT | IDLE/SAFE | FREECAM | SEARCH | TRACKING | ARMED | FAULT |
|------------|------|-----------|---------|--------|----------|-------|-------|
| **BOOT**       | —    | ✓         | —       | —      | —        | —     | ✓     |
| **IDLE/SAFE**  | —    | —         | ✓       | ✓      | —        | —     | ✓     |
| **FREECAM**    | —    | ✓         | —       | ✓      | —        | —     | ✓     |
| **SEARCH**     | —    | ✓         | —       | —      | ✓        | —     | ✓     |
| **TRACKING**   | —    | ✓         | —       | ✓      | —        | ✓     | ✓     |
| **ARMED**      | —    | ✓         | —       | —      | ✓        | —     | ✓     |
| **FAULT**      | ✓    | ✓         | —       | —      | —        | —     | —     |

**AM7-L3-MODE-002**: State machine ASCII diagram shall be:

```
                              ┌─────────────┐
                              │    BOOT     │
                              └──────┬──────┘
                                     │ (init OK)
                                     ▼
                              ┌─────────────┐
                         ┌────│  IDLE/SAFE  │◄───┐
                         │    └──────┬──────┘    │
                    (request)       │(request)    │(cancel)
                         │          │             │
                         ▼          ▼             │
                    ┌─────────┐ ┌────────┐        │
                    │ FREECAM │ │ SEARCH │────────┤
                    └────┬────┘ └───┬────┘        │
                         │          │(lock valid)  │
                         │          ▼             │
                         │    ┌──────────┐        │
                         │    │ TRACKING │◄───────┤
                         │    └────┬─────┘        │
                         │         │(lock+stable) │
                         │         ▼              │
                         │    ┌────────┐          │
                         └────│ ARMED  │──────────┘
                              └───┬────┘
                                  │
                    (any fault)   │
                ┌─────────────────┘
                ▼
         ┌─────────────┐
         │    FAULT    │
         └──────┬──────┘
                │
         (power cycle / manual reset)
                │
                └──────────────────────┘
```

**AM7-L3-MODE-003**: Entry conditions for each state shall be:
- BOOT: power-on detected
- IDLE/SAFE: BOOT complete, no faults
- FREECAM: operator request from IDLE/SAFE
- SEARCH: operator request from FREECAM or IDLE/SAFE
- TRACKING: valid target lock from SEARCH
- ARMED: valid lock + stable timing + no faults + operator authorization from TRACKING
- FAULT: any fault detected from any state

**AM7-L3-MODE-004**: Allowed actions for each state shall be:
- BOOT: hardware init, memory lock, thread spawn, self-test
- IDLE/SAFE: status monitoring, logging, accept mode requests
- FREECAM: accept operator gimbal commands, stream video, reject fire authorization
- SEARCH: scan FOV, detect targets, acquire first valid target
- TRACKING: maintain lock, update gimbal commands, monitor confidence/stability
- ARMED: enable interlock transition (inhibit→enable on valid fire request), maintain lock
- FAULT: force inhibit, hold gimbal, log fault, reject all commands except reset

**AM7-L3-MODE-005**: Prohibited actions for each state shall be:
- BOOT: accept operator input, drive gimbal, enable interlock
- IDLE/SAFE: drive gimbal, enable interlock, acquire targets
- FREECAM: automatic target lock, enable interlock
- SEARCH: enable interlock, track multiple targets
- TRACKING: enable interlock, lose lock without transition
- ARMED: transition from any state except TRACKING, maintain lock on target loss
- FAULT: enable interlock, drive gimbal, accept commands (except reset)

**AM7-L3-MODE-006**: Exit conditions for each state shall be:
- BOOT: init complete → IDLE/SAFE; init failure → FAULT
- IDLE/SAFE: operator request → FREECAM/SEARCH; fault → FAULT
- FREECAM: operator request → SEARCH/IDLE/SAFE; fault → FAULT
- SEARCH: valid lock → TRACKING; cancel → IDLE/SAFE; fault → FAULT
- TRACKING: lock valid+stable+armed → ARMED; lock lost → SEARCH; cancel → IDLE/SAFE; fault → FAULT
- ARMED: disarm → IDLE/SAFE; lock lost → TRACKING; fault → FAULT
- FAULT: power cycle → BOOT; manual reset → IDLE/SAFE

**AM7-L3-MODE-007**: Safety posture for each state shall be:
- BOOT: inhibit (GPIO low)
- IDLE/SAFE: inhibit (GPIO low)
- FREECAM: inhibit (GPIO low)
- SEARCH: inhibit (GPIO low)
- TRACKING: inhibit (GPIO low)
- ARMED: enable permitted (GPIO high on valid fire authorization)
- FAULT: inhibit (GPIO low, latched)

**AM7-L3-MODE-008**: Logging requirements for each state shall be:
- BOOT: log entry with result (pass/fail), boot time, self-test results
- IDLE/SAFE: log entry on state entry, mode requests received
- FREECAM: log entry, operator gimbal commands (rate-limited to 1 Hz)
- SEARCH: log entry, target detections (confidence, coordinates)
- TRACKING: log entry, lock status (confidence, stability metrics)
- ARMED: log entry, authorization state, interlock transition
- FAULT: log entry with fault ID, timestamp, source state, fault context

**AM7-L3-MODE-009**: No state shall transition directly to ARMED except TRACKING. Verification: state machine unit test with all state→ARMED transitions attempted; only TRACKING→ARMED succeeds.

**AM7-L3-MODE-010**: ARMED state entry shall require: (a) valid target lock (confidence ≥ 95%), (b) stable timing (jitter ≤ 5% at 99.9th percentile over 10M cycles), (c) zero active faults, (d) explicit operator authorization (two-key sequence or sustained button ≥ 2 seconds). Verification: ARMED entry test with each condition individually violated.

**AM7-L3-MODE-011**: Any fault shall transition immediately to FAULT state from any source state. Verification: fault injection test from each of 6 non-FAULT states; all transition to FAULT within 8.33ms.

**AM7-L3-MODE-012**: FAULT state shall always force interlock inhibit (GPIO low) regardless of prior state. Verification: fault injection from ARMED state; oscilloscope confirms GPIO transition low within 100ns.

---

### 10.5 Target Selection Logic

**AM7-L1-TGT-001**: System shall implement two target acquisition modes: automatic (SEARCH state) and manual (FREECAM or SEARCH state).

**AM7-L2-TGT-001**: Automatic acquisition in SEARCH state shall: (a) scan FOV in raster or spiral pattern, (b) evaluate each detected object per AM7-L2-VIS-004, (c) select first target meeting confidence ≥ 95% and stability ≥ 3 consecutive frames.

**AM7-L2-TGT-002**: Manual acquisition shall allow operator to select target via cursor overlay on video feed. Manual selection shall be accepted only in FREECAM or SEARCH state. Manual selection shall require validation per AM7-L2-TGT-004 before lock.

**AM7-L2-TGT-003**: Target selection shall require: (a) confidence ≥ 95% per AM7-L2-VIS-004, (b) stability ≥ 3 consecutive frames with consistent position (Δposition ≤ 2 pixels), (c) range data available (if equipped), (d) target within gimbal motion envelope per AM7-L2-ACT-002.

**AM7-L2-TGT-004**: Manual target selection shall require validation before lock: system shall confirm target meets AM7-L2-TGT-003 criteria over 3 consecutive frames. Validation failure shall reject selection and return to FREECAM or SEARCH.

**AM7-L3-TGT-001**: Target rejection conditions shall be: (a) confidence < 95%, (b) stability < 3 frames, (c) range data stale or invalid per AM7-L3-SAFE-002, (d) target outside gimbal envelope, (e) target occluded > 50%, (f) target exits FOV. Rejection shall log event with reason code.

**AM7-L3-TGT-002**: Lock confirmation shall require: (a) continuous detection in ≥ 95% of frames over 250ms window, (b) predicted position matches measured position (Δ ≤ 5 pixels), (c) confidence maintained ≥ 95%. Lock confirmation shall be logged.

**AM7-L3-TGT-003**: De-selection logic shall transition from TRACKING to SEARCH on: (a) target occluded > 1 second, (b) target exits FOV, (c) confidence drops < 90% for > 250ms, (d) operator cancel command. De-selection shall log event and clear lock state.

**AM7-L3-TGT-004**: Target handoff between automatic and manual shall be supported. Operator may override automatic selection with manual selection in SEARCH or TRACKING state. Manual override shall re-validate target per AM7-L2-TGT-004.

---

### 10.6 Timing & Isolation Guarantees

**AM7-L1-TIM-ISO-001**: Operator control path shall be non-authoritative. Real-time control loop shall retain final authority over all actuator commands.

**AM7-L2-TIM-ISO-001**: Real-time loop (vision_pipeline, track_compute, actuation_output) shall not be delayed by: (a) operator UI rendering, (b) network I/O, (c) disk I/O, (d) logging operations. Verification: WCET measurement with operator path saturated vs. idle; Δ ≤ 5%.

**AM7-L2-TIM-ISO-002**: Operator commands shall be sampled, not awaited. Real-time loop shall read latest operator command from ring buffer; no blocking waits. Verification: code inspection confirms non-blocking read (O(1) return).

**AM7-L2-TIM-ISO-003**: Worst-case execution time (WCET) per AM7-L2-TIM-002 shall remain unchanged under Ethernet stress. Verification: WCET measurement per AM7-L2-TIM-002 with saturated Ethernet traffic (1 Gbps line rate); WCET ≤ 5.0ms maintained.

**AM7-L3-TIM-ISO-001**: Ethernet stress test shall saturate link with: (a) ping flood (1000 packets/sec), (b) iperf3 UDP stream at 900 Mbps, (c) operator input at 120 Hz. Real-time loop WCET shall be measured during stress.

**AM7-L3-TIM-ISO-002**: WCET proof shall compare: (a) baseline WCET (no operator load), (b) stressed WCET (operator load + Ethernet saturation). Pass criteria: stressed WCET ≤ baseline WCET + 5%. Evidence: statistical WCET report with 100M+ samples for both conditions.

---

### 10.7 Logging & Auditability

**AM7-L1-LOG-OP-001**: System shall log all remote operation events to audit log per ICD-004.

**AM7-L2-LOG-OP-001**: Mode changes shall be logged with: timestamp (CLOCK_MONOTONIC_RAW), source state, destination state, trigger (operator/fault/automatic), operator ID (if applicable).

**AM7-L2-LOG-OP-002**: Operator input events shall be logged with: timestamp, input type (mouse/keyboard/scroll), value, bounded/rate-limited flag. Logging shall be rate-limited to 1 Hz to prevent log flooding.

**AM7-L2-LOG-OP-003**: Target selection events shall be logged with: timestamp, method (automatic/manual), confidence, coordinates, validation result, target ID.

**AM7-L2-LOG-OP-004**: Arm/disarm transitions shall be logged with: timestamp, authorization source (operator ID), interlock state (inhibit/enable), preconditions met (lock valid, timing stable, faults clear).

**AM7-L2-LOG-OP-005**: Faults during remote operation shall be logged with: timestamp, fault ID, source state, fault context (operator input value, target state, network status), action taken.

**AM7-L3-LOG-OP-001**: All logs shall use existing single timebase (CLOCK_MONOTONIC_RAW) per AM7-L3-TIM-001. Verification: log timestamp analysis confirms monotonic increase, no backward steps.

**AM7-L3-LOG-OP-002**: Logging shall never block. Write latency shall be ≤ 10μs worst-case. Verification: logging latency measurement under log buffer saturation.

**AM7-L3-LOG-OP-003**: All log entries shall be traceable to requirement IDs. Log event IDs shall map to AM7-L2-LOG-OP-* and AM7-L3-MODE-* requirements. Verification: traceability audit from log event ID to requirement.

---

### 10.8 Explicit Non-Goals

**AM7-L1-NONGOAL-001**: System non-goals shall be explicitly documented to prevent misinterpretation of system intent.

**AM7-L2-NONGOAL-001**: System is not a remote weapon. System shall not be configured, modified, or deployed for remote weapon applications.

**AM7-L2-NONGOAL-002**: System does not perform autonomous engagement. System shall not autonomously authorize fire or engage targets without explicit operator authorization.

**AM7-L2-NONGOAL-003**: System does not allow remote firing. Fire authorization requires local interlock; remote operator may request authorization but cannot directly trigger fire.

**AM7-L2-NONGOAL-004**: System is for educational and experimental control systems development only. System shall not be deployed in safety-critical, military, or certification-bound applications.

---

## 11. Requirements Traceability Matrix
| AM7-L1-SYS-001 | AM7-L2-SYS-001 | AM7-ICR-MECH-001 | Inspection: drawing review | Planned |
| AM7-L1-SYS-002 | AM7-L2-SYS-002 | AM7-L3-SYS-002 | Test: IP67 chamber (optional) | Planned |
| AM7-L1-SYS-003 | AM7-L2-TIM-001 | AM7-L3-TIM-001 | Test: sustained rate measurement | Planned |
| AM7-L1-SYS-003 | AM7-L2-TIM-002 | AM7-L3-TIM-002 | Analysis: measurement-based WCET | Planned |
| AM7-L1-SYS-003 | AM7-L2-TIM-003 | AM7-L3-TIM-003 | Test: jitter statistical analysis | Planned |
| AM7-L1-SYS-003 | AM7-L2-TIM-004 | AM7-L3-TIM-004 | Test: CPU utilization monitoring | Planned |
| — | AM7-L2-SYS-003 | AM7-L3-SYS-003 | Inspection: timing bounds verification | Planned |
| AM7-L1-SYS-004 | AM7-L2-VIS-001 | AM7-L3-VIS-002 | Analysis: algorithm review | Planned |
| AM7-L1-SYS-004 | — | AM7-L3-TIM-004 | Analysis: loop bound verification | Planned |
| AM7-L1-COM-001 | — | AM7-L3-ENV-001 | Test: thermal monitoring | Planned |
| AM7-L1-COM-002 | — | AM7-L3-EMC-001 | Test: EMI pre-compliance (optional) | Planned |
| AM7-L1-COM-003 | AM7-L2-SAFE-002 | AM7-L3-SAFE-003 | Analysis: fault tree analysis | Planned |
| AM7-L1-COM-003 | AM7-L2-SAFE-003 | AM7-L3-SAFE-004 | Analysis: reliability calculation | Planned |
| AM7-L1-COM-003 | AM7-L2-SAFE-004 | AM7-L3-SAFE-005 | Analysis: CCF checklist | Planned |
| AM7-L1-COM-003 | AM7-L2-SAFE-005 | AM7-L3-SAFE-006 | Analysis: PFH calculation | Planned |
| AM7-L1-SEC-001 | AM7-L2-SEC-001 | AM7-L3-SEC-001 | Test: HMAC authentication test | Planned |
| AM7-L1-SEC-001 | AM7-L2-SEC-001 | AM7-ICR-CAM-001 | Test: camera authentication test | Planned |
| AM7-L1-SEC-001 | AM7-L2-SEC-001 | AM7-ICR-ACT-001 | Test: gimbal authentication test | Planned |
| AM7-L1-SEC-002 | AM7-L2-SEC-002 | AM7-L3-SEC-002 | Test: secure boot test | Planned |
| AM7-L1-SEC-003 | AM7-L2-SEC-003 | AM7-ICR-LOG-001 | Test: log integrity test | Planned |
| AM7-L1-SEC-004 | AM7-L2-SEC-004 | AM7-L3-SEC-004 | Test: replay attack test | Planned |
| AM7-L1-SEC-005 | AM7-L2-SEC-005 | AM7-L3-SEC-005 | Test: firmware update test | Planned |
| AM7-L2-VIS-002 | — | AM7-L3-TIM-001 | Test: clock source verification | Planned |
| AM7-L2-VIS-003 | — | AM7-L3-VIS-001 | Test: zero-copy verification | Planned |
| AM7-L2-VIS-004 | — | AM7-L3-VIS-004 | Test: detection rate test | Planned |
| AM7-L2-ACT-001 | — | AM7-ICR-ACT-001 | Test: timing measurement | Planned |
| AM7-L2-ACT-002 | — | AM7-L3-ACT-001 | Test: constraint enforcement | Planned |
| AM7-L2-ACT-003 | — | AM7-L3-ACT-002 | Test: latency measurement | Planned |
| AM7-L2-SAFE-001 | — | AM7-L3-SAFE-001 | Test: fault response time | Planned |
| AM7-L2-SAFE-006 | — | AM7-L3-SAFE-002 | Test: range data revocation | Planned |
| AM7-L2-SAFE-007 | — | AM7-L3-SAFE-005 | Test: self-test coverage | Planned |
| AM7-L2-SAFE-002 | — | AM7-L3-SAFE-013 | Test: single fault injection | Planned |
| — | AM7-L2-SYS-003 | AM7-L3-MEM-001 | Inspection: memory map configuration | Planned |
| — | AM7-L2-SYS-003 | AM7-L3-MEM-002 | Test: stack overflow injection | Planned |
| — | AM7-L2-SYS-003 | AM7-L3-MEM-003 | Test: buffer overflow injection | Planned |
| AM7-L1-OS-001 | AM7-L2-OS-001 | AM7-L3-OS-001 | Test: boot log inspection | Planned |
| AM7-L1-OS-001 | AM7-L2-OS-002 | AM7-L3-OS-002 | Test: network interface enumeration | Planned |
| AM7-L1-OS-002 | AM7-L2-OS-003 | AM7-L3-OS-003 | Test: systemctl service verification | Planned |
| AM7-L1-OS-002 | AM7-L2-OS-004 | AM7-L3-OS-004 | Test: boot time measurement | Planned |
| AM7-L1-CTL-001 | AM7-L2-CTL-001 | AM7-L3-CTL-001 | Test: IPC latency measurement | Planned |
| AM7-L1-CTL-002 | AM7-L2-CTL-002 | AM7-L3-CTL-002 | Analysis: binary dependency check | Planned |
| AM7-L1-CTL-002 | AM7-L2-CTL-003 | AM7-L3-CTL-003 | Test: thread isolation verification | Planned |
| AM7-L1-IF-001 | AM7-L2-IF-001 | AM7-L3-IF-001 | Test: Ethernet loss injection | Planned |
| AM7-L1-IF-002 | AM7-L2-IF-002 | AM7-L3-IF-002 | Test: input latency measurement | Planned |
| AM7-L1-IF-002 | AM7-L2-IF-003 | AM7-L3-IF-003 | Test: invalid input injection | Planned |
| AM7-L1-IF-002 | AM7-L2-IF-004 | AM7-L3-IF-004 | Test: buffer overflow injection | Planned |
| AM7-L1-IF-002 | AM7-L2-IF-005 | AM7-L3-IF-005 | Test: state-command matrix coverage | Planned |
| AM7-L1-IF-002 | AM7-L2-IF-006 | AM7-L3-IF-006 | Test: link status monitoring | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-001 | AM7-L3-MODE-001 | Analysis: state transition table | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-002 | AM7-L3-MODE-002 | Test: BOOT state transition | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-003 | AM7-L3-MODE-003 | Test: IDLE/SAFE state transition | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-004 | AM7-L3-MODE-004 | Test: FREECAM state transition | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-005 | AM7-L3-MODE-005 | Test: SEARCH state transition | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-006 | AM7-L3-MODE-006 | Test: TRACKING state transition | Planned |
| AM7-L1-MODE-001 | AM7-L2-MODE-007 | AM7-L3-MODE-007 | Test: ARMED state transition | Planned |
| AM7-L1-MODE-002 | AM7-L2-MODE-007 | AM7-L3-MODE-008 | Test: FAULT state transition | Planned |
| AM7-L1-MODE-002 | AM7-L2-MODE-007 | AM7-L3-MODE-009 | Test: ARMED-only-from-TRACKING | Planned |
| AM7-L1-MODE-002 | AM7-L2-MODE-007 | AM7-L3-MODE-010 | Test: ARMED entry conditions | Planned |
| AM7-L1-MODE-002 | AM7-L2-MODE-007 | AM7-L3-MODE-011 | Test: fault→FAULT transition | Planned |
| AM7-L1-MODE-002 | AM7-L2-MODE-007 | AM7-L3-MODE-012 | Test: FAULT inhibit enforcement | Planned |
| AM7-L1-TGT-001 | AM7-L2-TGT-001 | AM7-L3-TGT-001 | Test: automatic acquisition | Planned |
| AM7-L1-TGT-001 | AM7-L2-TGT-002 | AM7-L3-TGT-002 | Test: manual acquisition | Planned |
| AM7-L1-TGT-001 | AM7-L2-TGT-003 | AM7-L3-TGT-003 | Test: target rejection | Planned |
| AM7-L1-TGT-001 | AM7-L2-TGT-004 | AM7-L3-TGT-004 | Test: lock confirmation | Planned |
| AM7-L1-TGT-001 | AM7-L2-TGT-004 | AM7-L3-TGT-005 | Test: de-selection logic | Planned |
| AM7-L1-TIM-ISO-001 | AM7-L2-TIM-ISO-001 | AM7-L3-TIM-ISO-001 | Test: Ethernet stress WCET | Planned |
| AM7-L1-TIM-ISO-001 | AM7-L2-TIM-ISO-002 | AM7-L3-TIM-ISO-002 | Analysis: WCET comparison | Planned |
| AM7-L1-LOG-OP-001 | AM7-L2-LOG-OP-001 | AM7-L3-LOG-OP-001 | Test: mode change logging | Planned |
| AM7-L1-LOG-OP-001 | AM7-L2-LOG-OP-002 | AM7-L3-LOG-OP-002 | Test: input event logging | Planned |
| AM7-L1-LOG-OP-001 | AM7-L2-LOG-OP-003 | AM7-L3-LOG-OP-003 | Test: target selection logging | Planned |
| AM7-L1-LOG-OP-001 | AM7-L2-LOG-OP-004 | AM7-L3-LOG-OP-004 | Test: arm/disarm logging | Planned |
| AM7-L1-LOG-OP-001 | AM7-L2-LOG-OP-005 | AM7-L3-LOG-OP-005 | Test: fault logging | Planned |
| AM7-L1-NONGOAL-001 | AM7-L2-NONGOAL-001 | — | Inspection: non-goal compliance | Planned |
| AM7-L1-NONGOAL-001 | AM7-L2-NONGOAL-002 | — | Inspection: non-goal compliance | Planned |
| AM7-L1-NONGOAL-001 | AM7-L2-NONGOAL-003 | — | Inspection: non-goal compliance | Planned |
| AM7-L1-NONGOAL-001 | AM7-L2-NONGOAL-004 | — | Inspection: non-goal compliance | Planned |
| AM7-L1-TGT-CLASS-001 | AM7-L2-TGT-CLASS-001 | AM7-L3-TGT-CLASS-001 | Test: calibration target detection | Planned |
| AM7-L1-TGT-CLASS-001 | AM7-L2-TGT-CLASS-002 | AM7-L3-TGT-CLASS-002 | Test: kinetic target tracking | Planned |
| AM7-L2-VIS-005 | — | AM7-L3-VIS-005 | Test: libcamera frame acquisition | Planned |
| AM7-L2-VIS-006 | — | AM7-L3-VIS-006 | Test: OpenCV processing pipeline | Planned |
| AM7-L2-VIS-007 | — | AM7-L3-VIS-007 | Test: zero-copy libcamera→OpenCV | Planned |
| AM7-L2-VIS-008 | — | AM7-L3-VIS-008 | Test: CSRT 120Hz tracking | Planned |
| AM7-L2-VIS-009 | — | AM7-L3-VIS-009 | Test: NEON/GPU acceleration | Planned |
| AM7-L2-BALL-001 | — | AM7-L3-BALL-001 | Test: ballistic calculation | Planned |
| AM7-L2-BALL-002 | — | AM7-L3-BALL-002 | Test: config.json profile load | Planned |
| AM7-L2-BALL-003 | — | AM7-L3-BALL-003 | Test: aim point offset calculation | Planned |
| AM7-L2-BALL-004 | — | AM7-L3-BALL-004 | Test: ballistic < 1ms timing | Planned |
| AM7-L2-HUD-001 | — | AM7-L3-HUD-001 | Test: HUD telemetry output | Planned |
| AM7-L2-HUD-002 | — | AM7-L3-HUD-002 | Test: HUD overlay content | Planned |
| AM7-L2-HUD-003 | — | AM7-L3-HUD-003 | Analysis: remote rendering architecture | Planned |
| AM7-L2-HUD-004 | — | AM7-L3-HUD-004 | Test: 120Hz telemetry update | Planned |
| — | AM7-L2-BUILD-001 | AM7-L3-BUILD-001 | Analysis: CMake build system | Planned |
| — | AM7-L2-BUILD-002 | AM7-L3-BUILD-002 | Analysis: C++17 standard | Planned |
| — | AM7-L2-BUILD-003 | AM7-L3-BUILD-003 | Analysis: compiler toolchain | Planned |
| — | AM7-L2-BUILD-004 | AM7-L3-BUILD-004 | Analysis: NEON compiler flags | Planned |
| — | AM7-L2-OPT-001 | AM7-L3-OPT-001 | Analysis: NEON SIMD usage | Planned |
| — | AM7-L2-OPT-002 | AM7-L3-OPT-002 | Test: VideoCore GPU utilization | Planned |
| — | AM7-L2-OPT-003 | AM7-L3-OPT-003 | Test: determinism with GPU offload | Planned |
| — | AM7-L2-OPT-004 | AM7-L3-OPT-004 | Analysis: performance comparison | Planned |
| — | AM7-L2-FUSION-001 | AM7-L3-FUSION-001 | Test: Fusion HAT I2C communication | Planned |
| — | AM7-L2-FUSION-002 | AM7-L3-FUSION-002 | Test: PWM servo control | Planned |
| — | AM7-L2-FUSION-003 | AM7-L3-FUSION-003 | Test: C++ library interface | Planned |
| AM7-L2-SAFE-002 | — | AM7-L3-SAFE-010 | Analysis: unified C++17 architecture | Planned |
| AM7-L2-SAFE-002 | — | AM7-L3-SAFE-011 | Test: self-monitoring thread | Planned |
| AM7-L2-SAFE-002 | — | AM7-L3-SAFE-012 | Test: I2C interlock output | Planned |
| AM7-L2-SAFE-002 | — | AM7-L3-SAFE-013 | Test: I2C servo command inhibit | Planned |

---

## 12. Verification and Validation Matrix

### 12.1 Timing Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-TIM-001 | Test | TIM-001-A | 120Hz ±1% sustained 30min at 25°C ±10°C, CPU ≤80% | Log file with timestamp trace |
| AM7-L2-TIM-002 | Analysis | TIM-002-A | WCET report ≤ 5.0ms from interrupt entry to GPIO toggle | Statistical WCET report (100M+ samples) |
| AM7-L2-TIM-003 | Test | TIM-003-A | 99.9th percentile jitter ≤ 5% of 8.33ms over 10M cycles | Statistical analysis report |
| AM7-L2-TIM-004 | Test | TIM-004-A | CPU utilization ≤ 70% avg, ≤ 85% peak | /proc/stat trace |
| AM7-L3-TIM-001 | Analysis | TIM-005-A | Binary analysis confirms no unauthorized time source calls | objdump / nm report |
| AM7-L3-TIM-002 | Test | TIM-006-A | Zero allocations detected over 10M cycles with LD_PRELOAD hooks | Allocation trace log |
| AM7-L3-TIM-003 | Test | TIM-007-A | All critical threads report SCHED_FIFO, correct priorities | sched_getscheduler() dump |
| AM7-L3-TIM-004 | Analysis | TIM-008-A | Static analysis confirms all loops bounded | clang-tidy report |
| AM7-L3-TIM-005 | Test | TIM-009-A | VmLck shows all memory locked | /proc/self/status dump |

### 12.2 Vision Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-VIS-001 | Analysis | VIS-001-A | Algorithm review confirms only classical methods | Algorithm documentation |
| AM7-L2-VIS-002 | Test | VIS-002-A | Timestamp accuracy ≤ 100μs vs CLOCK_MONOTONIC_RAW | Oscilloscope capture or logic analyzer |
| AM7-L2-VIS-003 | Test | VIS-003-A | Pipeline latency ≤ 3.0ms frame start to track output | Timing trace |
| AM7-L2-VIS-004 | Test | VIS-004-A | Pd ≥ 95%, FAR ≤ 10⁻⁴ on target signature set | Detection statistics |
| AM7-L3-VIS-001 | Test | VIS-005-A | Memory instrumentation confirms zero memcpy copies | Valgrind/DRD report |
| AM7-L3-VIS-002 | Analysis | VIS-006-A | Zero high-severity static analysis findings | clang-tidy / Coverity report |
| AM7-L3-VIS-003 | Test | VIS-007-A | Buffer alignment verified at runtime | Alignment check log |
| AM7-L3-VIS-004 | Test | VIS-008-A | Watchdog timeout triggers fault within 10ms | Fault injection test |

### 12.3 Actuation Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-ACT-001 | Test | ACT-001-A | Command rate 120Hz ±1%, boundary sync ±50μs | Timing measurement |
| AM7-L2-ACT-002 | Test | ACT-002-A | Gimbal respects elevation, azimuth, velocity, acceleration limits | Motion trace |
| AM7-L2-ACT-003 | Test | ACT-003-A | Command latency ≤ 2.0ms compute to servo | Latency measurement |
| AM7-L3-ACT-001 | Analysis | ACT-004-A | Memory map confirms .rodata placement | Memory map dump |
| AM7-L3-ACT-002 | Test | ACT-005-A | Sequence gap triggers hold command | Sequence injection test |
| AM7-L3-ACT-003 | Test | ACT-006-A | Limit violation triggers fault | Limit injection test |

### 12.4 Safety Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-SAFE-001 | Test | SAFE-001-A | Fault→inhibit ≤ 8.33ms | Oscilloscope capture |
| AM7-L2-SAFE-002 | Test | SAFE-002-A | Single fault injection does not cause loss of safety function | FMEA test report |
| AM7-L2-SAFE-003 | Analysis | SAFE-003-A | Fault tree analysis confirms DCavg ≥ 90% | Fault tree report |
| AM7-L2-SAFE-004 | Analysis | SAFE-004-A | Reliability analysis confirms MTTFd ≥ 100 years (target) | Reliability report |
| AM7-L2-SAFE-005 | Analysis | SAFE-005-A | CCF checklist confirms ≥ 65 points (target) | CCF analysis report |
| AM7-L2-SAFE-006 | Analysis | SAFE-006-A | PFH calculation confirms < 10⁻⁷/hour (target) | PFH report |
| AM7-L2-SAFE-007 | Test | SAFE-007-A | Self-test executes at power-on and every 100ms | Self-test log |
| AM7-L3-SAFE-001 | Test | SAFE-008-A | Power loss, brownout, reset, fault all trigger inhibit | Power-loss test |
| AM7-L3-SAFE-002 | Test | SAFE-009-A | Stale/invalid range data revokes fire auth within 1ms | Fault injection test |
| AM7-L3-SAFE-003 | Analysis | SAFE-010-A | Code review confirms diverse implementations | Code comparison report |
| AM7-L3-SAFE-004 | Test | SAFE-011-A | Channel mismatch detected within 100μs | Oscilloscope capture |
| AM7-L3-SAFE-005 | Test | SAFE-012-A | Watchdog timeout triggers reset + inhibit | Watchdog test |
| AM7-L3-SAFE-006 | Test | SAFE-013-A | Fault register latched, non-clearable except power cycle | Fault latch test |
| AM7-L3-SAFE-010 | Analysis | SAFE-014-A | MISRA C:2012 compliance (target) | clang-tidy report |
| AM7-L3-SAFE-011 | Analysis | SAFE-015-A | No unsafe blocks in Rust safety code | cargo audit report |
| AM7-L3-SAFE-012 | Analysis | SAFE-016-A | Timing analysis confirms ≤ 100μs comparator | Timing report |
| AM7-L3-SAFE-013 | Test | SAFE-017-A | Single channel failure results in inhibit | Fault injection test |
| AM7-L3-SAFE-014 | Test | SAFE-018-A | Self-test coverage ≥ 95% | Coverage report |

### 12.5 Security Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-SEC-001 | Test | SEC-001-A | HMAC verification passes for valid, fails for invalid | Authentication test |
| AM7-L2-SEC-002 | Test | SEC-002-A | ECDSA signature verified on all firmware images | Signature test |
| AM7-L2-SEC-003 | Test | SEC-003-A | Audit log contains all required events with valid HMAC | Log audit |
| AM7-L2-SEC-004 | Test | SEC-004-A | Replay attack with old sequence number rejected | Replay test |
| AM7-L2-SEC-005 | Test | SEC-005-A | Session timeout after 300 seconds inactivity | Timeout test |
| AM7-L2-SEC-006 | Test | SEC-006-A | Keys not stored in plaintext | Key storage test |
| AM7-L3-SEC-001 | Test | SEC-007-A | HMAC computed over correct fields | HMAC verification |
| AM7-L3-SEC-002 | Test | SEC-008-A | Boot halts on invalid signature (if secure boot enabled) | Secure boot test |
| AM7-L3-SEC-003 | Test | SEC-009-A | Log entry format correct, HMAC valid | Log format test |
| AM7-L3-SEC-004 | Test | SEC-010-A | Sequence gap > 1000 triggers fault | Sequence test |
| AM7-L3-SEC-005 | Test | SEC-011-A | Invalid signature, downgrade, corrupt update rejected | Update test |
| AM7-L3-SEC-006 | Test | SEC-012-A | Keys generated with ≥ 256 bits entropy, not extractable | RNG test |

### 12.6 Memory Protection Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L3-MEM-001 | Analysis | MEM-001-A | Memory map confirms correct configuration | /proc/self/maps dump |
| AM7-L3-MEM-002 | Test | MEM-002-A | Stack overflow injection triggers fault | Stack test |
| AM7-L3-MEM-003 | Test | MEM-003-A | Buffer overflow injection detected | Buffer test |

### 12.7 Environmental Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-ENV-001 | Test | ENV-001-A | Functional at -40°C to +55°C (target, optional chamber) | Thermal monitoring log |
| AM7-L1-ENV-002 | Test | ENV-002-A | Functional at 5% to 95% RH | Humidity monitoring log |
| AM7-L1-ENV-003 | Test | ENV-003-A | Functional at sea level to 4500m | Altitude test (optional) |

### 12.8 Integration Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-ICR-CAM-001 | Test | INT-001-A | Camera frames delivered with valid timestamps | Integration test |
| AM7-ICR-CAM-002 | Test | INT-002-A | Camera delivery non-blocking under backpressure | Backpressure test |
| AM7-ICR-ACT-001 | Test | INT-003-A | Gimbal commands issued at 120Hz | Integration test |
| AM7-ICR-SAFE-001 | Test | INT-004-A | Interlock responds correctly to all fault conditions | Integration test |
| AM7-ICR-LOG-001 | Test | INT-005-A | Logging non-blocking, 64KB buffer, circular overwrite | Integration test |

### 12.9 OS Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-OS-001 | Test | OS-001-A | Raspberry Pi OS Lite (64-bit) confirmed | /etc/os-release dump |
| AM7-L1-OS-001 | Test | OS-002-A | PREEMPT_RT kernel confirmed | uname -r, cat /proc/version |
| AM7-L2-OS-001 | Analysis | OS-003-A | No desktop/X11/browser packages installed | dpkg -l audit |
| AM7-L2-OS-002 | Test | OS-004-A | HDMI and Bluetooth disabled | vcgencmd, hciconfig |
| AM7-L2-OS-002 | Test | OS-005-A | Ethernet sole operational interface | ip link show, ethtool |
| AM7-L2-OS-003 | Test | OS-006-A | Unnecessary services disabled | systemctl is-enabled audit |
| AM7-L2-OS-004 | Test | OS-007-A | Boot time ≤ 15 seconds | systemd-analyze blame |
| AM7-L3-OS-001 | Test | OS-008-A | Boot log inspection | /var/log/boot.log, dmesg |
| AM7-L3-OS-002 | Test | OS-009-A | Power/latency before/after comparison | Current measurement, latency trace |
| AM7-L3-OS-003 | Test | OS-010-A | Network interface binding | ip link, ethtool eth0 |
| AM7-L3-OS-004 | Test | OS-011-A | Service disablement persistence | Reboot + systemctl is-enabled |

### 12.10 Control Architecture Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-CTL-001 | Analysis | CTL-001-A | C++ toolchain confirmed | g++ --version, build system audit |
| AM7-L1-CTL-002 | Test | CTL-002-A | Separate process confirmed | ps aux, /proc/[pid]/status |
| AM7-L2-CTL-001 | Test | CTL-003-A | IPC mechanism confirmed (socket or ring buffer) | ss -xl, ipcs -m |
| AM7-L2-CTL-002 | Test | CTL-004-A | Real-time threads not blocked | Thread timing trace under load |
| AM7-L2-CTL-003 | Analysis | CTL-005-A | No web dependencies | ldd, strings binary analysis |
| AM7-L2-CTL-004 | Analysis | CTL-006-A | No VNC/RDP installed | dpkg -l, systemctl list-unit-files |
| AM7-L3-CTL-001 | Test | CTL-007-A | IPC latency ≤ 1ms at 99.9th percentile | Timestamp delta over 1M samples |
| AM7-L3-CTL-002 | Analysis | CTL-008-A | Binary dependency analysis | ldd, nm, strings report |
| AM7-L3-CTL-003 | Test | CTL-009-A | Thread isolation confirmed | /proc/[pid]/task inspection |

### 12.11 Remote Interface Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-IF-001 | Test | IF-001-A | Direct Ethernet connection confirmed | ethtool, ip neighbor |
| AM7-L1-IF-002 | Test | IF-002-A | Input rate limiting confirmed | High-rate input injection test |
| AM7-L2-IF-001 | Test | IF-003-A | Input sampling ≥ 120 Hz | Input timestamp analysis |
| AM7-L2-IF-002 | Test | IF-004-A | Input latency ≤ 10ms at 99th percentile | Timestamp delta measurement |
| AM7-L2-IF-003 | Test | IF-005-A | Mouse drag → gimbal mapping | Input/output correlation test |
| AM7-L2-IF-004 | Test | IF-006-A | Scroll wheel → zoom mapping | Zoom rate measurement |
| AM7-L2-IF-005 | Test | IF-007-A | Keyboard keybinds functional | Keybind coverage test |
| AM7-L2-IF-006 | Test | IF-008-A | No direct actuator commands | Code inspection, state machine audit |
| AM7-L3-IF-001 | Test | IF-009-A | Ethernet loss → IDLE/SAFE within 100ms | Cable disconnect test |
| AM7-L3-IF-002 | Test | IF-010-A | Invalid input ignored + logged | Invalid input injection |
| AM7-L3-IF-003 | Test | IF-011-A | Buffer overflow → drop + assert | Buffer saturation test |
| AM7-L3-IF-004 | Test | IF-012-A | State machine filters commands | State-command matrix test |
| AM7-L3-IF-005 | Test | IF-013-A | Link status monitored at ≥ 10 Hz | Link polling trace |

### 12.12 State Machine Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-MODE-001 | Analysis | MODE-001-A | State machine explicit transitions only | Code inspection, model review |
| AM7-L1-MODE-002 | Test | MODE-002-A | 7 mandatory states present | State enumeration audit |
| AM7-L2-MODE-001 | Test | MODE-003-A | BOOT → IDLE/SAFE on success | Boot test |
| AM7-L2-MODE-001 | Test | MODE-004-A | BOOT → FAULT on failure | Fault injection during boot |
| AM7-L2-MODE-002 | Test | MODE-005-A | IDLE/SAFE maintains inhibit | Interlock GPIO measurement |
| AM7-L2-MODE-003 | Test | MODE-006-A | FREECAM accepts gimbal commands | Command/response test |
| AM7-L2-MODE-004 | Test | MODE-007-A | SEARCH acquires first valid target | Target acquisition test |
| AM7-L2-MODE-005 | Test | MODE-008-A | TRACKING maintains lock | Lock stability test |
| AM7-L2-MODE-006 | Test | MODE-009-A | ARMED only from TRACKING | State transition test |
| AM7-L2-MODE-007 | Test | MODE-010-A | FAULT forces inhibit from any state | Fault injection from all states |
| AM7-L3-MODE-001 | Analysis | MODE-011-A | State transition table complete | Transition matrix audit |
| AM7-L3-MODE-009 | Test | MODE-012-A | ARMED-only-from-TRACKING enforced | All-state→ARMED attempt test |
| AM7-L3-MODE-010 | Test | MODE-013-A | ARMED entry conditions validated | Condition violation test |
| AM7-L3-MODE-011 | Test | MODE-014-A | Fault→FAULT within 8.33ms | Fault injection timing test |
| AM7-L3-MODE-012 | Test | MODE-015-A | FAULT inhibit enforcement | GPIO oscilloscope capture |

### 12.13 Target Selection Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-TGT-001 | Test | TGT-001-A | Automatic and manual modes present | Mode enumeration test |
| AM7-L2-TGT-001 | Test | TGT-002-A | Automatic acquisition scans FOV | Scan pattern verification |
| AM7-L2-TGT-002 | Test | TGT-003-A | Manual selection via cursor overlay | Cursor selection test |
| AM7-L2-TGT-003 | Test | TGT-004-A | Selection requires confidence + stability | Threshold validation test |
| AM7-L2-TGT-004 | Test | TGT-005-A | Manual selection validated before lock | Validation timing test |
| AM7-L3-TGT-001 | Test | TGT-006-A | Rejection conditions enforced | Rejection criteria test |
| AM7-L3-TGT-002 | Test | TGT-007-A | Lock confirmation criteria met | Lock confirmation test |
| AM7-L3-TGT-003 | Test | TGT-008-A | De-selection logic functional | Target loss transition test |
| AM7-L3-TGT-004 | Test | TGT-009-A | Auto/manual handoff functional | Handoff test |

### 12.14 Timing Isolation Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-TIM-ISO-001 | Analysis | TIM-ISO-001-A | Operator path non-authoritative | Code inspection, authority audit |
| AM7-L2-TIM-ISO-001 | Test | TIM-ISO-002-A | Real-time loop not delayed by UI/I/O | WCET comparison under load |
| AM7-L2-TIM-ISO-002 | Analysis | TIM-ISO-003-A | Commands sampled, not awaited | Code inspection (non-blocking read) |
| AM7-L2-TIM-ISO-003 | Test | TIM-ISO-004-A | WCET unchanged under Ethernet stress | Stressed WCET measurement |
| AM7-L3-TIM-ISO-001 | Test | TIM-ISO-005-A | Ethernet saturation test | iperf3 + ping flood + input test |
| AM7-L3-TIM-ISO-002 | Analysis | TIM-ISO-006-A | WCET proof (baseline vs. stressed) | Statistical WCET report (100M+ samples) |

### 12.15 Remote Logging Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-LOG-OP-001 | Test | LOG-OP-001-A | Remote operation events logged | Log content inspection |
| AM7-L2-LOG-OP-001 | Test | LOG-OP-002-A | Mode changes logged correctly | Mode change log audit |
| AM7-L2-LOG-OP-002 | Test | LOG-OP-003-A | Input events logged (rate-limited) | Input log audit |
| AM7-L2-LOG-OP-003 | Test | LOG-OP-004-A | Target selection logged | Target log audit |
| AM7-L2-LOG-OP-004 | Test | LOG-OP-005-A | Arm/disarm transitions logged | Authorization log audit |
| AM7-L2-LOG-OP-005 | Test | LOG-OP-006-A | Faults logged with context | Fault log audit |
| AM7-L3-LOG-OP-001 | Test | LOG-OP-007-A | CLOCK_MONOTONIC_RAW timebase | Timestamp monotonicity test |
| AM7-L3-LOG-OP-002 | Test | LOG-OP-008-A | Logging ≤ 10μs worst-case | Logging latency measurement |
| AM7-L3-LOG-OP-003 | Analysis | LOG-OP-009-A | Logs traceable to requirements | Traceability audit |

### 12.16 Target Class Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L1-TGT-CLASS-001 | Test | TGT-CLASS-001-A | Calibration target detected | Detection test with 50×50mm Red/Yellow target |
| AM7-L1-TGT-CLASS-001 | Test | TGT-CLASS-002-A | Kinetic target tracked | Tracking test with 80mm helicopter model |
| AM7-L2-TGT-CLASS-001 | Test | TGT-CLASS-003-A | Contrast calibration successful | Contrast ratio measurement |
| AM7-L2-TGT-CLASS-002 | Test | TGT-CLASS-004-A | Template matching validated | Template match confidence ≥ 95% |

### 12.17 Vision/CSRT Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-VIS-005 | Test | VIS-009-A | libcamera RAW10 acquisition | Frame capture test |
| AM7-L2-VIS-006 | Test | VIS-010-A | OpenCV processing pipeline | Algorithm output verification |
| AM7-L2-VIS-007 | Test | VIS-011-A | Zero-copy libcamera→OpenCV | Memory instrumentation (no memcpy) |
| AM7-L2-VIS-008 | Test | VIS-012-A | CSRT 120Hz tracking | Tracking loop frequency measurement |
| AM7-L2-VIS-009 | Test | VIS-013-A | NEON acceleration active | Binary analysis (NEON instructions) |
| AM7-L2-VIS-009 | Test | VIS-014-A | GPU acceleration active | vcgencmd/PMU counter analysis |
| AM7-L2-VIS-009 | Test | VIS-015-A | CSRT latency ≤ 3ms | End-to-end timing measurement |
| AM7-L2-VIS-009 | Test | VIS-016-A | 1536×864@120Hz sustained | Sustained rate test 30min |

### 12.18 Ballistic Engine Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-BALL-001 | Test | BALL-001-A | Ballistic calculation correct | Known solution comparison |
| AM7-L2-BALL-002 | Test | BALL-002-A | config.json profiles loaded | Profile load verification |
| AM7-L2-BALL-003 | Test | BALL-003-A | Aim point offset calculated | Offset vs. range verification |
| AM7-L2-BALL-004 | Test | BALL-004-A | Ballistic < 1ms timing | Execution time measurement |

### 12.19 HUD Telemetry Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-HUD-001 | Test | HUD-001-A | Telemetry data output | Socket message capture |
| AM7-L2-HUD-002 | Test | HUD-002-A | HUD overlay content correct | Reticle, box, status verification |
| AM7-L2-HUD-003 | Analysis | HUD-003-A | Remote rendering architecture | Architecture review |
| AM7-L2-HUD-004 | Test | HUD-004-A | 120Hz telemetry update | Update rate measurement |

### 12.20 Build System Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L3-BUILD-001 | Analysis | BUILD-001-A | CMake build system | CMakeLists.txt audit |
| AM7-L3-BUILD-002 | Analysis | BUILD-002-A | C++17 standard | Compiler flags audit |
| AM7-L3-BUILD-003 | Analysis | BUILD-003-A | GCC/clang ARM64 toolchain | Compiler version check |
| AM7-L3-BUILD-004 | Analysis | BUILD-004-A | NEON optimization flags | -mfpu=neon flag verification |

### 12.21 Optimization Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L3-OPT-001 | Analysis | OPT-001-A | NEON SIMD usage | objdump NEON instruction audit |
| AM7-L3-OPT-002 | Test | OPT-002-A | VideoCore GPU utilization | vcgencmd/PMU counter analysis |
| AM7-L3-OPT-003 | Test | OPT-003-A | Determinism with GPU offload | Jitter comparison (GPU vs. CPU) |
| AM7-L3-OPT-004 | Analysis | OPT-004-A | Performance comparison | Benchmark report (100M+ samples) |

### 12.22 Fusion HAT Verification

| Requirement ID | Verification Method | Test Case | Pass/Fail Criteria | Evidence Artifact |
|----------------|---------------------|-----------|-------------------|-------------------|
| AM7-L2-FUSION-001 | Test | FUSION-001-A | I2C communication functional | I2C read/write test |
| AM7-L2-FUSION-002 | Test | FUSION-002-A | PWM servo control | Servo pulse width measurement |
| AM7-L2-FUSION-003 | Test | FUSION-003-A | C++ library interface | Library API test |
| AM7-L2-FUSION-004 | Test | FUSION-004-A | Interlock servo command | Inhibit/Enable command test |
| AM7-L2-FUSION-005 | Test | FUSION-005-A | I2C timeout fault | I2C disconnect fault injection |

---

## 13. Environmental and EMC Design Targets

### 13.1 Environmental Testing (Optional, Desktop Verification)

| Method | Design Target | Verification (Desktop) | Pass/Fail Criteria |
|--------|---------------|------------------------|-------------------|
| High Temp | +55°C operational | Thermal monitoring under load, throttling check | No thermal throttling, functional |
| Low Temp | -20°C operational (reduced from -40°C) | Cold start test (optional) | Functional post cold soak |
| Shock | 10g half-sine (reduced from 40g) | Drop test from 1m onto foam | No damage, functional |
| Vibration | Random, 5-100Hz, 0.5g RMS | Shaker table (optional) or field test | No damage, functional |
| Dust/Water | IP54 (reduced from IP67) | Visual inspection, spray test | No ingress, functional |

### 13.2 EMC Pre-Compliance (Optional)

| Requirement | Test | Limit (Pre-Compliance) | Pass/Fail Criteria |
|-------------|------|------------------------|-------------------|
| Conducted Emissions | LISN measurement | Quasi-peak per CISPR 32 Class B | Below limit + 6dB margin |
| Radiated Emissions | Near-field probe scan | Relative to CISPR 32 | No hotspots > target |
| Radiated Susceptibility | RF generator + antenna | 3 V/m (reduced from 20 V/m) | Functional during/after |
| ESD | ESD gun (optional) | ±4kV contact, ±8kV air | No latchup, functional |

**Design Practices:**
- Shielded enclosure recommended (aluminum with EMI gaskets)
- 360° connector bonding for all I/O
- Ferrite beads on all cables
- Ground plane on PCB (if custom HAT used)
- SSD boot recommended over SD card (better EMI and reliability)

---

## 14. Personal Development Notes

### 14.1 Raspberry Pi 5 Platform Notes

**Known Limitations:**
1. **No ECC Memory:** Single-bit errors are undetected. For learning purposes only.
2. **No Lockstep Cores:** CPU faults are undetected until software manifestation.
3. **Linux is Not Real-Time:** PREEMPT_RT reduces latency but does not guarantee determinism.
4. **V4L2 Driver Latency:** Camera driver latency is unbounded under system load.
5. **GPU/ISP Memory Contention:** VideoCore GPU and ISP share DRAM bandwidth with CPU.
6. **MicroSD Write Latency:** Unbounded; use high-endurance cards with care.
7. **No Safety Manual:** Broadcom does not provide safety manual for BCM2712.

**User Hardware Configuration:**
1. Raspberry Pi 5 4GB RAM (user-provided hardware)
2. Boot from 128GB MicroSD card
3. Camera: Raspberry Pi Camera Module 3 (naked, no cooler or cable extensions)
4. Audio: Fusion HAT+ onboard speaker/microphone (for situational awareness)
5. Gimbal/Interlock Control: SunFounder Fusion HAT+ (I2C address 0x17, GD32 MCU)
   - 12-channel PWM (16-bit resolution, 50Hz for servos)
   - Channels 0-1: Gimbal servos (elevation, azimuth)
   - Channel 2: Interlock servo
6. I2C Connection: GPIO2 (SDA) / GPIO3 (SCL) on 40-pin header; 10KΩ pull-ups onboard Fusion HAT+

**Recommended Mitigations:**
1. Enable PREEMPT_RT kernel (`sudo rpi-update` for RT kernel)
2. Monitor CPU temperature; add active cooling if thermal throttling observed
3. Use high-endurance MicroSD card (industrial grade recommended)
4. Regular backups of boot image
5. Monitor I2C bus integrity with logic analyzer during development

### 14.2 Measurement-Based WCET Methodology

Since static WCET analysis (absint aiT) is not available for Cortex-A72, use statistical WCET:

1. **Instrumentation:** Add GPIO toggle at interrupt entry and exit
2. **Data Collection:** Capture 100M+ samples using logic analyzer or oscilloscope
3. **Stress Conditions:**
   - Memory bandwidth saturation (parallel dd reads)
   - CPU load (stress-ng --cpu 4)
   - Thermal throttling (run until 80°C+)
   - Interrupt storm (high-rate GPIO toggling)
4. **Analysis:** Fit to Generalized Extreme Value (GEV) distribution
5. **WCET Bound:** Derive at p=1-10⁻⁹ confidence level

**Tools:**
- Logic analyzer: Saleae Logic 8 or DSView
- Oscilloscope: Rigol DS1054Z or equivalent
- Stress tools: stress-ng, phoronix-test-suite

### 14.3 Learning Objectives

This project is designed to teach:
1. Real-time systems design and analysis
2. Safety architecture principles (ISO 13849, IEC 61508)
3. Embedded Linux real-time programming
4. Computer vision for embedded systems (libcamera, OpenCV, CSRT)
5. Hardware acceleration (ARM NEON SIMD, VideoCore VII GPU)
6. Cryptographic authentication for embedded systems
7. Requirements engineering and traceability
8. Verification and validation methodologies
9. C++17 modern embedded development
10. I2C peripheral interfacing (servo control, sensor integration)

**This is NOT a certification-bound project.** Use it to learn principles that can be applied to certified systems on appropriate hardware.

---

## 15. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-01 | Original | Initial release |
| 2.0 | 2026-03-02 | Audit Remediation | Complete rewrite per hostile audit findings |
| 3.0 | 2026-03-02 | Pi 5 Educational Revision | Removed certification claims, adapted for personal development |
| 4.0 | 2026-03-03 | Remote Control & Modes Extension | Added Section 10: Operating Modes, Control Interfaces, and Remote Operation (79 new requirements); Added ICD-005: Operator Control Application Interface; Updated traceability and V&V matrices |
| 5.0 | 2026-03-03 | Hardware & Architecture Revision | Unified C++17 architecture (removed Rust/C dual-channel); Added libcamera/OpenCV/CSRT vision stack; Added Physical Target Definitions; Added HUD Telemetry (ICD-006); Added Fusion HAT interface (ICD-007); Changed interlock to I2C servo command; Added CMake build system; Added NEON/GPU optimization requirements; Updated hardware config (4GB RPi, 128GB MicroSD, naked camera, Audio HAT) |

**Document Classification:** PERSONAL DEVELOPMENT / EDUCATIONAL USE

---

## 16. End of Document

**Document Classification:** PERSONAL DEVELOPMENT / EDUCATIONAL USE
**Platform:** Raspberry Pi 5 4GB with 128GB MicroSD, RPi Camera Module 3 (naked), SunFounder Fusion HAT, Audio HAT
**Total Requirements:** 208 (L1: 33, L2: 82, L3: 93)
**Total Test Cases:** 170+
**Traceability Coverage:** 100%
**V&V Coverage:** 100% (target)

**Architecture Summary:**
- Unified C++17 implementation (no Rust/C dual-channel)
- libcamera + OpenCV hybrid vision stack
- CSRT tracker at 120Hz with NEON/GPU acceleration
- I2C servo command interlock (not discrete GPIO)
- CMake build system with ARM NEON optimization

**State Machine Summary:**
- 7 mandatory states: BOOT, IDLE/SAFE, FREECAM, SEARCH, TRACKING, ARMED, FAULT
- Explicit transitions only; no spontaneous or implicit transitions
- ARMED only from TRACKING; any fault → FAULT; FAULT forces inhibit

**Physical Target Definitions:**
- Calibration/Debug Target: 50×50 mm bifacial (Red/Yellow), matte finish
- Primary Kinetic Target: 80×30×30 mm miniature helicopter, charcoal grey fuselage, navy canopy

**Verification Coverage Statement:**
- OS Verification: 11 test cases (OS-001-A through OS-011-A)
- Control Architecture Verification: 9 test cases (CTL-001-A through CTL-009-A)
- Remote Interface Verification: 13 test cases (IF-001-A through IF-013-A)
- State Machine Verification: 15 test cases (MODE-001-A through MODE-015-A)
- Target Selection Verification: 9 test cases (TGT-001-A through TGT-009-A)
- Timing Isolation Verification: 6 test cases (TIM-ISO-001-A through TIM-ISO-006-A)
- Remote Logging Verification: 9 test cases (LOG-OP-001-A through LOG-OP-009-A)
- Vision/CSRT Verification: 8 test cases (VIS-009-A through VIS-016-A)
- Build System Verification: 4 test cases (BUILD-001-A through BUILD-004-A)
- Optimization Verification: 4 test cases (OPT-001-A through OPT-004-A)
- Target Class Verification: 4 test cases (TGT-CLASS-001-A through TGT-CLASS-004-A)
- HUD Telemetry Verification: 4 test cases (HUD-001-A through HUD-004-A)
- Fusion HAT Verification: 5 test cases (FUSION-001-A through FUSION-005-A)

**NOT FOR SAFETY-CRITICAL USE.** This specification is for educational purposes only. Do not deploy in any application where failure could cause injury, death, or property damage.

---

# Mk VII Fire Control System Technical Specification
## Target Detection & Engagement Subsystem — Rev. 1.0

---

## 1. System Overview

### 1.1 Purpose
The Mk VII FCS provides autonomous detection, tracking, and precision engagement of static miniature aerial targets (80×30×30 mm helicopters) at ranges up to 2 meters. The system demonstrates advanced fire control capabilities including multi-modal ballistics, predictive gimbal compensation, and redundant target verification.

### 1.2 Core Philosophy
This is not a "point-and-shoot" tracking system. The Mk VII implements a **state-aware engagement pipeline** with explicit verification checkpoints, environmental ballistics modeling, and mechanical latency compensation. Complexity is intentional—every subsystem demonstrates real-world FCS engineering principles at demonstrator scale.

### 1.3 Engagement Modes
- **Kinetic Mode**: Direct fire (>0 -700 m/s muzzle velocity) for immediate impact
- **Drop Mode**: Gravity delivery (0 m/s muzzle velocity) for top-down attack and obstacle clearance

---

## 2. Hardware Architecture

### 2.1 Processing Platform
| Parameter | Specification |
|-----------|--------------|
| Primary Processor | Raspberry Pi 5 (BCM2712, ARM Cortex-A76 ×4 @ 2.4 GHz) |
| Memory | 4 GB LPDDR4X |
| OS | Raspberry Pi OS Lite (64-bit) |
| Real-time Constraints | Soft real-time, 120 Hz sensor input, state machine at ~30 Hz effective |

### 2.2 Imaging Subsystem
| Parameter | Specification |
|-----------|--------------|
| Sensor | 4K×4K CMOS (cropped to 1536×864 ROI) |
| Frame Rate | 120 Hz (free-run, latest-frame-grab) |
| Interface | MIPI CSI-2 |
| Lens | Fixed-focus, 3.6mm focal length, IR-cut filter optional |
| FOV | ~90° horizontal (sufficient for 2m range coverage) |

### 2.3 Gimbal System
| Parameter | Specification |
|-----------|--------------|
| Axes | 2-DOF (azimuth/yaw, elevation/pitch) |
| Drive | MG996R digi high-torque servos |
| Latency | 50 ms (measured, end-to-end command-to-settle) |
| Settling Time | <100 ms for 90° step |
| Angular Resolution | 0.1° |
| Control Interface | PWM via Fusion HAT+ (I2C→GD32 MCU→16-bit PWM) |

### 2.4 Rangefinding
| Parameter | Specification |
|-----------|--------------|
| Sensor | UART Laser Rangefinder |
| Update Rate | 20 Hz (50 ms period) |
| Range | 0.1 - 10 m |
| Accuracy | ±10 mm |
| Interface | UART (9600 baud, NMEA or binary protocol) |

### 2.5 Absent Environmental Sensors
| Sensor | Purpose | Interface |
|--------|---------|-----------|
| BME280 | Temperature, humidity, pressure (air density calc) | I2C |
| Anemometer (optional) | Wind estimation for drop mode | ADC or I2C |

**These sensors will NOT be used.** The system is designed to use standard air density (configurable via config.json) rather than live sensor readings.

### 2.6 Effector System
| Parameter | Specification |
|-----------|--------------|
| Type | 1 singular PWM servo |
| Velocity Range | 0 - 10000 m/s (configurable) |
| Velocity Resolution | 10 m/s steps |
| Trigger | PWM output (active high, 10 ms pulse) |
| Safety Interlock | Software switch + software ARM state requirement |

---

## 3. Software Architecture

### 3.1 Processing Model
**Asynchronous, latest-frame processing with state machine serialization.**

The system does not process every frame. Instead:
1. Camera free-runs at 120 Hz, frames buffered in circular buffer (latest only kept)
2. Main loop grabs latest available frame when state machine demands processing
3. Detection/tracking runs serially on demand, not continuously
4. State transitions gate algorithm execution (no wasted cycles)

### 3.2 Threading Model
| Thread | Priority | Function |
|--------|----------|----------|
| Main Control | High | State machine, ballistics, gimbal command |
| Vision Worker | Medium | Detection, tracking (on-demand invocation) |
| Sensor I/O | High | LRF, environmental sensors (interrupt-driven) |
| Gimbal Driver | High | Real-time position control (1 kHz inner loop) |
| Logging | Low | Data recording, telemetry output |

### 3.3 Coordinate Systems
- **Image Coordinates**: (u,v) in pixels, origin top-left, 1536×864
- **Camera Coordinates**: (Xc, Yc, Zc) in meters, Z forward, X right, Y down
- **Gimbal Coordinates**: (Az, El) in degrees, Azimuth 0 = forward, Elevation 0 = horizontal
- **World Coordinates**: (Xw, Yw, Zw) in meters, arbitrary origin at system boot

---

## 4. State Machine Specification

### 4.1 State Definitions
```
[IDLE] → [ALERT] → [LOCATED] → [HOMING] → [LOCKED] → [ARMED] → [FIRE] → [IDLE]
           ↑___________↓____________↑___________↓ (recovery paths)
```

| State | Description | Entry Condition | Exit Condition |
|-------|-------------|-----------------|----------------|
| **IDLE** | System ready, scanning | Initialization complete | Detection algorithm reports target |
| **ALERT** | Possible detection, unconfirmed | First detection frame | Second consecutive detection (spatial proximity) OR timeout |
| **LOCATED** | Target confirmed, position known | Two consecutive detections within proximity gate | CSRT tracker initialized successfully |
| **HOMING** | Gimbal moving to track target | Tracker active | Gimbal position error < 2° AND gimbal velocity < 5°/s |
| **LOCKED** | Tracking stable, verifying identity | Gimbal settled | Re-detection confirms target identity AND environmental checks pass |
| **ARMED** | Solution computed, firing authorized | Ballistics solution valid with P(hit) > 0.95 | Gimbal aligned with solution vector AND fire command issued |
| **FIRE** | Effector triggered | ARMED state alignment confirmed | Return to IDLE after 100 ms |

### 4.2 State Transition Logic

#### IDLE → ALERT
- **Trigger**: Detection algorithm returns bounding box with confidence > 0.7
- **Action**: Log timestamp, store detection features, start 50 ms timeout

#### ALERT → LOCATED (or IDLE)
- **Success**: Second detection within 100 ms, bounding box center within 50 pixels of first
- **Failure**: Timeout or spatial mismatch → return to IDLE
- **Action**: Initialize CSRT tracker with confirmed bounding box

#### LOCATED → HOMING
- **Trigger**: CSRT tracker initialized, target centroid computed
- **Action**:
  - Compute target position in world coordinates using LRF range (or last known)
  - Command gimbal to target position + 70 ms velocity prediction
  - Begin gimbal lag compensation

#### HOMING → LOCKED (or LOCATED)
- **Success**:
  - Gimbal position error < 2° (angular distance to target)
  - Gimbal velocity < 5°/s (settled)
  - Sustained for 3 consecutive frames (25 ms at 120 Hz)
- **Failure**: Tracker lost OR timeout > 500 ms → return to LOCATED
- **Action**:
  - Capture reference template of current view
  - Begin re-detection verification

#### LOCKED → ARMED (or HOMING)
- **Success**:
  - Re-detection (NCC template match) confirms target identity (match score > 0.85)
  - LRF range agrees with expected size (±20% tolerance)
  - Ballistics solution computed with P(hit) > 0.95
  - No maneuver detected (acceleration < 2 m/s²)
- **Failure**: Identity mismatch OR range disagreement OR low confidence → return to HOMING
- **Action**:
  - Select engagement mode (kinetic vs. drop) based on target aspect and environment
  - Compute lead angles with 70 ms gimbal lag compensation
  - Populate fire control solution buffer

#### ARMED → FIRE (or LOCKED)
- **Success**:
  - Gimbal aligned with solution vector (error < 0.5°)
  - Alignment sustained for 20 ms
  - Safety interlock engaged
- **Failure**: Alignment lost OR maneuver detected OR timeout > 100 ms → return to LOCKED
- **Action**: Trigger effector, log firing solution, transition to FIRE

#### FIRE → IDLE
- **Trigger**: Effector pulse complete (10 ms)
- **Action**: Log engagement data, reset all state variables, return to scanning

### 4.3 Timeout Values
| Transition | Timeout | Purpose |
|------------|---------|---------|
| ALERT | 50 ms | Prevent lingering on false positives |
| HOMING | 500 ms | Limit time spent on unreachable targets |
| LOCKED | 200 ms | Force re-verification or recovery |
| ARMED | 100 ms | Minimize exposure to target maneuver |

### 4.4 Recovery Behaviors
| From State | Recovery To | Condition |
|------------|-------------|-----------|
| ALERT | IDLE | Single detection, no confirmation |
| LOCATED | IDLE | Tracker initialization failure |
| HOMING | LOCATED | Tracker lost, gimbal fault |
| LOCKED | HOMING | Re-detection failure |
| ARMED | LOCKED | Alignment lost, safety interlock disengaged |

---

## 5. Vision Algorithms

### 5.1 Detection (ALERT/LOCATED States)
**Algorithm**: ORB (Oriented FAST and Rotated BRIEF)

**Parameters**:
```cpp
nfeatures = 300
scaleFactor = 1.2
nlevels = 8
edgeThreshold = 31
firstLevel = 0
WTA_K = 2
scoreType = ORB_HARRIS
patchSize = 31
fastThreshold = 20
```

**Pipeline**:
1. Convert to grayscale, CLAHE contrast enhancement (clip limit 2.0, grid 8×8)
2. Detect ORB keypoints and 32-byte descriptors
3. Brute-force Hamming distance match against template database
4. Ratio test (best/second_best < 0.75), RANSAC homography verification (min 10 inliers)
5. Output bounding box, match score, centroid (u,v)

**Template Database**:
- 30 real images × 15 augmentations = 450 templates
- Organized by yaw angle (20° increments, -180° to +180°)
- Stored as precomputed ORB descriptors (binary format)

### 5.2 Tracking (HOMING/LOCKED States)
**Algorithm**: CSRT (Channel and Spatial Reliability Tracking)

**Parameters**:
```cpp
psr_threshold = 0.035
filter_lr = 0.02
template_size = 200
```

**Failure detection**: PSR < 0.035 OR box area change > 50% → trigger recovery

### 5.3 Re-detection (LOCKED State)
**Algorithm**: Normalized Cross-Correlation (NCC) template matching

- `cv::matchTemplate()` with `TM_CCOEFF_NORMED`
- Threshold: match score > 0.85 for identity confirmation
- Input: current frame crop around CSRT prediction vs. reference template from HOMING→LOCKED transition

---

## 6. Gimbal Control & Latency Compensation

### 6.1 Predictive Command Generation
In HOMING state, gimbal commands are **predictive** (70 ms lead):
```
target_future = target_current + target_velocity × 0.070
gimbal_cmd_position = target_future
gimbal_cmd_velocity = target_velocity  // feedforward
```

### 6.2 Kalman Filter for Gimbal State Estimation
- **State vector**: `[θ_az, θ_el, ω_az, ω_el, α_az, α_el]`
- **Measurement**: Encoder feedback (position only)
- Used for: smooth velocity estimation, fault detection (measured vs. predicted divergence)

---

## 7. Ballistics Subsystem

### 7.1 Environmental Modeling
Standard air density (1.225 kg/m³ at sea level), configurable in config.json. No live sensor readings.

### 7.2 Engagement Mode Selection
```
IF target_aspect_ratio > 2.0 AND range < 1.5m → DROP mode
ELSE IF gimbal_elevation < 45° → KINETIC mode (700 m/s)
ELSE → DROP mode (minimum velocity for arc)
```

### 7.3 Trajectory Calculation
**Kinetic Mode** (flat fire): `t = range / velocity`, `Δh = 0.5 × g × t²`

**Drop Mode** (ballistic arc):
Solve for `(Vx, Vz)` given range `x` and height `z` to target:
```
z = Vz × t - 0.5 × g × t²
x = Vx × t
```
Select `t` minimizing total launch velocity. Convert to gimbal elevation: `el = atan2(Vz, Vx)`.

### 7.4 Lookup Table Architecture
**Precomputed 4D table** (stored as binary file, loaded at boot):
```c
#define RANGE_BINS   20   // 0.1 to 2.0 m, 0.1 m steps
#define ELEV_BINS    18   // -90 to +90°, 10° steps
#define VEL_BINS     71   // 0 to 700 m/s, 10 m/s steps
#define DENSITY_BINS  5   // 1.0 to 1.3 kg/m³ (fixed, single bin at 1.225)

typedef struct {
    int16_t az_lead;    // millidegrees
    int16_t el_lead;    // millidegrees
    uint8_t confidence; // 0-255
} SolutionEntry;

SolutionEntry ballistics_table[RANGE_BINS][ELEV_BINS][VEL_BINS][DENSITY_BINS];
// Total: ~500 KB
```

Runtime: quantize inputs → trilinear interpolation → output lead angles + confidence.

### 7.5 Monte Carlo Confidence Estimation
50 simulations with perturbed inputs (Gaussian):
- Range: σ = 10 mm, Muzzle velocity: σ = 5 m/s
- Air density: σ = 0.02 kg/m³, Gimbal alignment: σ = 0.1°

**Hit criterion**: Impact within 50% of target size (40 mm). P(hit) = hits/50. Require > 0.95 to transition to ARMED.

---

## 8. Data Management

### 8.1 Template Database Structure
```
/data/templates/
├── real/           # 30 real images at 20° yaw increments
├── augmented/      # 450 augmented images
└── descriptors/    # Precomputed ORB binary descriptors
```

### 8.2 Per-Engagement Log (JSON)
```json
{
  "timestamp": "ISO8601",
  "engagement_id": "uuid",
  "states": [{"state": "ALERT", "t": 0.0, "detection_conf": 0.85}, ...],
  "ballistics": {"mode": "KINETIC", "velocity": 700, "range": 1.85, ...},
  "environment": {"density": 1.225}
}
```

---

## 9. Verification & Validation

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| V&V-001 | Calibration target detection on white background | 100% detection |
| V&V-004 | Helicopter detection, yaw 0°, range 1m | ≥95% |
| V&V-006 | Helicopter detection, all yaw angles | ≥90% average |
| V&V-007 | State machine transition timing | All < specified timeouts |
| V&V-008 | Gimbal lag compensation accuracy | <1° prediction error at settle |
| V&V-009 | Ballistics accuracy (kinetic) | <10 mm impact error at 2m |
| V&V-010 | Ballistics accuracy (drop) | <20 mm impact error at 2m |
| V&V-011 | False positive rate | <1% on background-only sequences |
| V&V-012 | System latency (detection to fire) | <300 ms total |

---

## 10. Safety & Operational Constraints

### 10.1 Safety Interlocks
1. **Software ARM**: Explicit state requirement, resets on any fault
2. **Range gate**: No firing if LRF reports >3m
3. **Gimbal limit checks**: No commands beyond mechanical stops
4. **Timeout enforcement**: All states have hard timeout limits

### 10.2 Operational Limits
- Max range: 2.0 m, Min range: 0.3 m
- Target velocity: 0 m/s (static targets only)
- Environment: Indoor use only, 15-30°C

---

## 11. Appendices

### Appendix A: Coordinate Transformations
**Image → Camera**:
```
Xc = (u - cx) × Zc / fx
Yc = (v - cy) × Zc / fy
Zc = range from LRF (or estimated)
```
**Camera → Gimbal**:
```
Az = atan2(Xc, Zc) × 180/π
El = atan2(-Yc, sqrt(Xc² + Zc²)) × 180/π
```

### Appendix C: References
- Lukežič et al. (2018). "Discriminative Correlation Filter Tracker with Channel and Spatial Reliability." CVPR.
- Rublee et al. (2011). "ORB: An efficient alternative to SIFT or SURF." ICCV.
