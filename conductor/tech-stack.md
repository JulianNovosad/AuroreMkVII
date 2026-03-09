# AuroreMkVII - Technology Stack

## Core Technologies

### Programming Language
| Item | Version | Purpose |
|------|---------|---------|
| **C++** | C++17 | Primary language for real-time components |
| **JavaScript** | ES2022+ | Frontend HUD (aurore-link) |
| **Node.js** | ≥18 | Mock server and development tooling |

### Build System
| Item | Version | Purpose |
|------|---------|---------|
| **CMake** | ≥3.16 | Cross-platform build configuration |
| **GCC** | ≥12 | C++ compiler (native and cross-compile) |
| **Clang** | ≥15 | Alternative C++ compiler, static analysis |

## Target Platform

### Raspberry Pi 5 Deployment
| Component | Specification |
|-----------|---------------|
| **CPU** | Broadcom BCM2712, Quad-core Cortex-A76 @ 2.4GHz |
| **GPU** | VideoCore VII @ 800MHz |
| **RAM** | 4GB/8GB LPDDR4X-4267 |
| **OS** | Raspberry Pi OS (Bookworm) or Ubuntu 24.04 |
| **Kernel** | ≥6.8 with PREEMPT_RT (optional but recommended) |

### Host Development (x86_64)
| Component | Specification |
|-----------|---------------|
| **Architecture** | x86_64 (Intel/AMD) |
| **OS** | Linux (Ubuntu 24.04 recommended) |
| **Purpose** | Unit tests, development, CI/CD |

## Libraries and Dependencies

### C++ Dependencies
| Library | Minimum Version | Purpose |
|---------|-----------------|---------|
| **libcamera** | ≥0.3 | RAW10 frame capture, MIPI CSI-2 |
| **OpenCV** | ≥4.5 | Image processing, KCF tracker |
| **libgpiod** | ≥2.0 | GPIO line access (character device) |
| **Eigen3** | ≥3.4 | Linear algebra (ballistic solver) |

### JavaScript Dependencies
| Library | Version | Purpose |
|---------|---------|---------|
| **ws** | ≥8.0.0 | WebSocket server (mock-server.js) |
| **eslint** | ≥10.0 | Linting |
| **@eslint/js** | ≥10.0 | ESLint configuration |

### System Dependencies
| Library | Minimum Version | Purpose |
|---------|-----------------|---------|
| **glibc** | ≥2.39 | C standard library |
| **libwebp** | ≥1.3.2 | Image codec (CVE-2023-4863 fix) |
| **Linux Kernel** | ≥6.8.0 | System calls, real-time scheduling |

## Real-Time Infrastructure

### Threading Model
| Feature | Implementation |
|---------|----------------|
| **Scheduler** | SCHED_FIFO (POSIX real-time) |
| **Clock Source** | CLOCK_MONOTONIC_RAW |
| **Synchronization** | Lock-free SPSC ring buffers |
| **CPU Affinity** | Pinned to isolated cores (CPU 2-3) |

### Timing Specifications
| Parameter | Value |
|-----------|-------|
| **Frame Rate** | 120 Hz |
| **Frame Period** | 8.333ms ±50μs |
| **WCET Budget** | ≤5.0ms |
| **Jitter Budget** | ≤5% at 99.9th percentile |
| **Safety Monitor** | 1kHz (1ms period) |

## Hardware Interfaces

### Camera Interface
| Interface | Specification |
|-----------|---------------|
| **Protocol** | MIPI CSI-2 |
| **Format** | RAW10 (10-bit Bayer) |
| **Resolution** | 1536×864 |
| **Frame Rate** | 120 FPS |
| **Transfer** | DMA (zero-copy) |

### Gimbal Control
| Interface | Specification |
|-----------|---------------|
| **Protocol** | I2C (Inter-Integrated Circuit) |
| **Controller** | Fusion HAT+ (GD32 MCU) |
| **PWM Channels** | 16-bit resolution |
| **Update Rate** | Asynchronous, ≤10ms latency |

### Power Management
| Rail | Voltage | Purpose |
|------|---------|---------|
| **5V** | 4.75-5.25V | Raspberry Pi, Fusion HAT+ |
| **3.3V** | 3.0-3.6V | I2C peripherals, sensors |

## Development Tools

### Static Analysis
| Tool | Purpose |
|------|---------|
| **clang-tidy** | Code quality, best practices |
| **cppcheck** | Bug detection |
| **eslint** | JavaScript linting |

### Testing
| Tool | Purpose |
|------|---------|
| **GoogleTest** | C++ unit testing framework |
| **CTest** | Test orchestration |
| **gcov/lcov** | Code coverage analysis |

### Code Formatting
| Tool | Configuration |
|------|---------------|
| **clang-format** | `.clang-format` (LLVM-based) |
| **prettier** | JavaScript/TypeScript (optional) |

## Security Considerations

### Minimum Dependency Versions (CVE Remediation)
| Dependency | Minimum Version | CVE Addressed |
|------------|-----------------|---------------|
| OpenCV | ≥4.9.0 | CVE-2024-2167, CVE-2024-3400 |
| libwebp | ≥1.3.2 | CVE-2023-4863 (Critical) |
| glibc | ≥2.39-1ubuntu3 | CVE-2024-2961 |
| Linux Kernel | ≥6.8.0-25 | CVE-2024-26581 |

### Security Practices
- No hardcoded credentials or API keys
- WebSocket authentication (when deployed)
- Input validation on all external data
- Regular dependency updates via `apt` and `npm`
