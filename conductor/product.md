# AuroreMkVII - Product Guide

## Product Vision

AuroreMkVII is a real-time vision-based fire control system (FCS) designed for the Raspberry Pi 5 platform. It processes 1536×864 RAW10 frames at 120Hz with deterministic timing and comprehensive safety monitoring.

**Disclaimer:** This project is for educational and skill acquisition purposes only. The Raspberry Pi 5 platform is NOT suitable for safety-critical, military, or certification-bound applications.

## Target Users

- **Primary:** Hobbyists and enthusiasts interested in real-time embedded systems
- **Secondary:** Students learning about computer vision, real-time systems, and embedded development
- **Tertiary:** Developers exploring ARM-based real-time applications

## Core Goals

1. **Real-Time Performance:** Achieve 120Hz frame processing with ≤5ms WCET budget
2. **Deterministic Timing:** Frame period tolerance of ±50μs at 99.9th percentile
3. **Safety Monitoring:** 1kHz safety monitor with comprehensive fault detection
4. **Zero-Copy Architecture:** No buffer copies between camera input and track output
5. **Educational Value:** Clean, well-documented code demonstrating real-time system design

## Key Features

### Vision Pipeline
- RAW10 frame capture via libcamera
- Real-time image preprocessing
- Color segmentation and target detection
- KCF (Kernelized Correlation Filter) tracking at 1-2ms execution time

### Ballistic Solver
- Precomputed trajectory tables
- Real-time lead calculation (azimuth/elevation)
- Probability of hit (p_hit) estimation

### Safety System
- 1kHz deadline watchdog
- Thread health monitoring
- Fail-safe state transitions
- CPU temperature and load monitoring

### Remote Interface (aurore-link)
- WebSocket-based telemetry streaming
- AC-130 style military HUD display
- Virtual joystick for gimbal control
- Real-time system status dashboard

## System Architecture

```
[libcamera RAW10] → vision_pipeline → LockFreeRingBuffer → track_compute
                                                      ↓
                                         LockFreeRingBuffer → actuation_output → Fusion HAT+ I2C
                                                      ↓
                                               safety_monitor (1kHz)
```

### Thread Model
| Thread | Priority | CPU | Period | Phase Offset |
|--------|----------|-----|--------|--------------|
| safety_monitor | 99 | 3 | 1ms | 0ms |
| actuation_output | 95 | 2 | 8.333ms | 4ms |
| vision_pipeline | 90 | 2 | 8.333ms | 0ms |
| track_compute | 85 | 2 | 8.333ms | 2ms |

## Success Metrics

- [ ] 120Hz frame processing with ≤5ms WCET
- [ ] Jitter ≤5% at 99.9th percentile
- [ ] Zero deadline misses under normal operation
- [ ] Clean build with zero clang-tidy warnings
- [ ] >80% unit test coverage for core modules
