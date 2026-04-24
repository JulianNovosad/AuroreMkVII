# AuroreMkVII Issue Report

Compiled issues/bugs found in source code requiring investigation.

---

## Codebase Statistics

| Metric | Count |
|--------|-------|
| Total C++ Lines | ~9,000 |
| stderr errors/warnings | 170 |
| errno checks | 55 |
| catch blocks | 13 |
| TODO/FIXME | 3 |
| abort() calls | 11 |
| throw statements | 9 |

**Estimated Total Issues: ~270+** (unique error paths, not all will trigger)

---

### 1. FATAL Aborts (10)

| Location | Issue |
|----------|-------|
| `src/drivers/camera_wrapper.cpp:372` | `std::abort()` - malformed control |
| `src/drivers/camera_wrapper.cpp:409` | `std::abort()` - invalid PISP refcount |
| `src/drivers/camera_wrapper.cpp:429` | `std::abort()` - DMA buffer too small |
| `src/drivers/camera_wrapper.cpp:637` | `std::abort()` - frame timeout |
| `src/drivers/camera_wrapper.cpp:646` | `std::abort()` - frame validation |
| `src/drivers/camera_wrapper.cpp:670` | `std::abort()` - DMA invalid |
| `src/drivers/camera_wrapper.cpp:820` | `std::abort()` - capture failure |
| `src/drivers/camera_wrapper.cpp:827` | `std::abort()` - no captured frames |
| `src/drivers/camera_wrapper.cpp:934` | `std::abort()` - frame not valid |
| `src/drivers/camera_wrapper.cpp:938` | `std::abort()` - contract violation |

---

### 2. Camera Errors (16)

| Location | Issue |
|----------|-------|
| `src/drivers/camera_wrapper.cpp:196` | GPU eglChooseConfig failed |
| `src/drivers/camera_wrapper.cpp:208` | GPU eglCreatePbufferSurface failed |
| `src/drivers/camera_wrapper.cpp:219` | GPU eglCreateContext failed |
| `src/drivers/camera_wrapper.cpp:226` | GPU eglMakeCurrent failed |
| `src/drivers/camera_wrapper.cpp:408` | FATAL: libcamera init failed - No camera found or hardware error |
| `src/drivers/camera_wrapper.cpp:472` | CameraManager start failed |
| `src/drivers/camera_wrapper.cpp:478` | No cameras found |
| `src/drivers/camera_wrapper.cpp:486` | Camera acquire failed |
| `src/drivers/camera_wrapper.cpp:495` | generateConfiguration failed |
| `src/drivers/camera_wrapper.cpp:510` | Configuration invalid |
| `src/drivers/camera_wrapper.cpp:519` | configure() failed |
| `src/drivers/camera_wrapper.cpp:532` | Buffer allocation failed |
| `src/drivers/camera_wrapper.cpp:547` | mmap failed |
| `src/drivers/camera_wrapper.cpp:559` | No capture requests created |
| `src/drivers/camera_wrapper.cpp:583` | camera->start() failed |

---

### 3. Main.cpp Errors/Warnings (26+)

| Location | Issue |
|----------|-------|
| `src/main.cpp:89` | Failed to set SCHED_FIFO for thread |
| `src/main.cpp:101` | Failed to set CPU affinity for thread |
| `src/main.cpp:116` | FATAL: Failed to lock memory (mlockall) |
| `src/main.cpp:140` | Warning: Failed to set memlock limit |
| `src/main.cpp:152` | Warning: Failed to set stack limit |
| `src/main.cpp:191` | Failed to initialize capabilities |
| `src/main.cpp:198` | Failed to set capabilities |
| `src/main.cpp:203` | Failed to set permitted capabilities |
| `src/main.cpp:209` | Failed to apply capabilities |
| `src/main.cpp:225` | Failed to drop GID |
| `src/main.cpp:231` | Failed to drop UID |
| `src/main.cpp:287` | FATAL: Failed to lock memory - cannot guarantee real-time |
| `src/main.cpp:294` | Warning: Failed to load config/config.json, using defaults |
| `src/main.cpp:329` | SAFETY ACTION fault code triggered |
| `src/main.cpp:340` | FATAL: Failed to drop privileges |
| `src/main.cpp:363` | Camera initialization failed |
| `src/main.cpp:390` | Warning: Could not create socket dir |
| `src/main.cpp:398` | Warning: HUD socket failed to start |
| `src/main.cpp:407` | Warning: MJPEG streamer failed to start |
| `src/main.cpp:415` | Warning: MJPEG USB streamer failed to start |
| `src/main.cpp:453` | Warning: Interlock initialization failed |
| `src/main.cpp:474` | Warning: failed to open LRF device |
| `src/main.cpp:499` | Warning: Failed to load descriptor file |
| `src/main.cpp:516` | DualCamera: WARN - USB stream failed |
| `src/main.cpp:563` | AuroreLink: HEARTBEAT TIMEOUT |
| `src/main.cpp:572` | AuroreLink: EMERGENCY_INHIBIT |
| `src/main.cpp:644` | Warning: YOLO26n model not loaded |
| `src/main.cpp:647` | Warning: ONNX Runtime not available |
| `src/main.cpp:731` | Vision deadline missed |
| `src/main.cpp:755` | Vision capture exceeded deadline |
| `src/main.cpp:905` | OpticalGate WARN: USB/MIPI misalignment |
| `src/main.cpp:987` | Track compute exceeded deadline |
| `src/main.cpp:1093` | FusionHat: I2C error threshold exceeded |
| `src/main.cpp:1249` | Actuation exceeded deadline |
| `src/main.cpp:1280` | Safety fault detected! |
| `src/main.cpp:1284` | Emergency stop active |
| `src/main.cpp:1348` | Thread did not terminate |

---

### 4. Safety/ Security Issues

| Location | Issue |
|----------|-------|
| `src/common/security.hpp` | Defines fault codes that trigger FAULT state |
| `src/state_machine/state_machine.cpp:158` | Invalid transition to ARMED |

---

### 5. Network Errors (8)

| Location | Issue |
|----------|-------|
| `src/network/mjpeg_streamer.cpp:42` | socket() failed |
| `src/network/mjpeg_streamer.cpp:51` | bind() failed |
| `src/network/mjpeg_streamer.cpp:60` | listen() failed |
| `src/network/mjpeg_streamer.cpp:118` | accept() error |
| `src/network/aurore_link_server.cpp:60` | failed to bind ports |
| `src/network/aurore_link_server.cpp:235` | Invalid sync word |
| `src/network/aurore_link_server.cpp:249` | HMAC verification failed |
| `src/network/aurore_link_server.cpp:278` | Replay attack detected |
| `src/network/aurore_link_server.cpp:393` | EMERGENCY_INHIBIT received |
| `src/network/aurore_link_server.cpp:518` | HEARTBEAT TIMEOUT |

---

### 6. I2C/ Hardware Errors (12+)

| Location | Issue |
|----------|-------|
| `src/drivers/fusion_hat.cpp:74` | I2C timeout on write |
| `src/drivers/fusion_hat.cpp:82` | I2C NACK on write |
| `src/drivers/fusion_hat.cpp:100` | I2C timeout on write |
| `src/drivers/fusion_hat.cpp:120` | I2C slow response on write |
| `src/drivers/fusion_hat.cpp:147` | I2C timeout on read |
| `src/drivers/fusion_hat.cpp:172` | I2C timeout on read |
| `src/drivers/fusion_hat.cpp:192` | I2C slow response on read |
| `src/drivers/fusion_hat.cpp:228` | Invalid configuration |
| `src/drivers/fusion_hat.cpp:286` | Device not connected |
| `src/drivers/fusion_hat.cpp:296` | Failed to enable channel |
| `src/drivers/fusion_hat.cpp:304` | Failed to set period |
| `src/safety/interlock_controller.cpp:40` | Failed to open GPIO memory device |
| `src/safety/interlock_controller.cpp:56` | Failed to map GPIO memory |
| `src/safety/interlock_controller.cpp:116` | Invalid interlock configuration |
| `src/safety/interlock_controller.cpp:127` | GPIO initialization failed |
| `src/safety/interlock_controller.cpp:374` | Interlock self-test FAILED |

---

### 7. LRF/Detector Errors (10)

| Location | Issue |
|----------|-------|
| `src/drivers/laser_rangefinder.cpp:230` | UART open failed |
| `src/drivers/laser_rangefinder.cpp:245` | tcgetattr failed |
| `src/drivers/laser_rangefinder.cpp:273` | tcsetattr failed |
| `src/drivers/laser_rangefinder.cpp:412` | UART read error |
| `src/drivers/laser_rangefinder.cpp:517` | CRC mismatch |
| `src/drivers/usb_camera.cpp:135` | Invalid configuration |
| `src/drivers/usb_camera.cpp:174` | No USB webcam detected |
| `src/drivers/usb_camera.cpp:196` | Failed to open device |
| `src/drivers/usb_camera.cpp:228` | Cannot start: not initialized |
| `src/drivers/usb_camera.cpp:237` | Failed to grab initial frame |
| `src/vision/orb_detector.cpp:35` | File size check |
| `src/vision/dual_camera_manager.cpp:25` | Error: NULL MIPI camera |
| `src/vision/dual_camera_manager.cpp:41` | USB camera init failed |
| `src/vision/dual_camera_manager.cpp:47` | USB camera start failed |
| `src/vision/dual_camera_manager.cpp:88` | USB camera timeout |
| `src/vision/dual_camera_manager.cpp:160` | USB camera disconnected |

---

### 8. Unhandled Exceptions (8)

| Location | Issue |
|----------|-------|
| `src/main.cpp:362` | catch CameraException |
| `src/drivers/camera_wrapper.cpp:880` | CameraException handler |
| `src/vision/orb_detector.cpp:42` | Catch cv::Exception |
| `src/vision/yolo26_detector.cpp:74` | Catch Ort::Exception |
| `src/sensors/imu_receiver.cpp:91` | Catch all |
| `src/sensors/imu_receiver.cpp:326` | Catch all |
| `src/common/config_loader.cpp:65` | JSON parse_error |
| `src/common/config_loader.cpp:102` | JSON type_error |
| `src/common/config_loader.cpp:121` | JSON type_error |
| `src/common/config_loader.cpp:140` | JSON type_error |
| `src/common/config_loader.cpp:160` | JSON type_error |

---

### 9. TODO Items (3)

| Location | Issue |
|----------|-------|
| `src/drivers/camera_wrapper.cpp:254` | GPU shader implementation |
| `src/drivers/camera_wrapper.cpp:308` | GPU-based RAW10→BGR888 conversion |
| `src/drivers/camera_wrapper.cpp:968` | Full GPU conversion |

---

### 10. Socket/Connection Issues

| Location | Issue |
|----------|-------|
| `src/common/hud_socket.cpp:40` | socket() failed |
| `src/common/hud_socket.cpp:47` | fcntl(F_GETFL) failed |
| `src/common/hud_socket.cpp:61` | bind() failed |
| `src/common/hud_socket.cpp:69` | chmod() failed |
| `src/common/hud_socket.cpp:76` | listen() failed |
| `src/common/hud_socket.cpp:119` | SO_PEERCRED failed |
| `src/common/hud_socket.cpp:160` | accept() failed |
| `src/common/command_socket.cpp:46` | socket() failed |
| `src/common/command_socket.cpp:58` | bind() failed |
| `src/common/command_socket.cpp:65` | listen() failed |
| `src/sensors/imu_receiver.cpp:120` | socket() failed |
| `src/sensors/imu_receiver.cpp:127` | setsockopt failed |
| `src/sensors/imu_receiver.cpp:148` | bind() failed |
| `src/sensors/imu_receiver.cpp:236` | select() error |
| `src/sensors/imu_receiver.cpp:252` | recvfrom() error |

---

## Priority Issues

### Critical (FATAL - causes system crash)
1. Camera aborts (10 locations) - camera_wrapper.cpp
2. Memory lock failures - main.cpp:116, 287
3. Privilege drop failure - main.cpp:340
4. Safety fault triggers - main.cpp:1280

### High (Causes degraded operation)
1. Vision deadline exceeded (multiple) - main.cpp
2. Track compute exceeded - main.cpp
3. I2C errors - fusion_hat.cpp
4. Stream connection failures - mjpeg_streamer.cpp
5. USB camera failures - usb_camera.cpp, dual_camera_manager.cpp

### Medium (Warnings - investigate)
1. Configuration warnings - config_loader.cpp
2. YOLO/ONNX not loaded - main.cpp:644, 647
3. Socket creation failures - various
4. TODO implementations - GPU/ shader

---

*Generated from codebase grep analysis*

---

## Current State Assessment (2026-04-24 session 2)

### Timeline
| Metric | Value |
|--------|-------|
| First commit | 2026-03-08 |
| Latest commit | 2026-04-24 |
| Total commits | ~163 |
| Development time | ~1.5 months |

### Issues Status
| Category | Count | Fixed |
|----------|-------|-------|
| FATAL Aborts | 11 | **11 (100%)** |
| Camera Errors | 16 | ~10 (62%) |
| Main.cpp Errors | 26+ | ~20 (77%) |
| Safety/Security | ~5 | **5 (100%)** |
| Network Errors | 8 | ~6 (75%) |
| I2C/Hardware | 12+ | ~7 (58%) |
| LRF/Detector | 10 | ~3 (30%) |
| Unhandled Exceptions | 8 | ~5 (62%) |
| TODO Items | 3 | **3 (100%)** |
| Socket/Connection | 11 | ~5 (45%) |
| **Total** | **~270+** | **~75 (28%)** |

### Completion by Area
| Area | Done | Remaining |
|------|------|-----------|
| Camera (color, streaming) | **90%** | WCET verify on hardware |
| Real-time performance (WCET ≤5ms) | **65%** | PISP dual-stream in place; measure |
| Safety/security (spec §3-4) | **90%** | ECDSA done (AM7-L2-SEC-002); only firmware update flow (AM7-L3-SEC-005) remains |
| Detection/tracking (YOLO26, KCF) | 50% | Performance tuning |
| Streaming/web interface | **85%** | Working |
| Hardware (I2C, FusionHAT, LRF) | **60%** | Slow-success bug fixed, 500µs retry backoff added |
| Error handling (`abort()` removal) | **100%** | Done 2026-04-23 |
| Config loading | **100%** | Done 2026-04-23 |
| Heartbeat timeout | **100%** | Done 2026-04-23 |
| HMAC authentication (AM7-L2-SEC-001) | **100%** | Done 2026-04-24 |
| Replay attack prevention (AM7-L2-SEC-004) | **100%** | Done 2026-04-24 |
| Session timeout (AM7-L2-SEC-005) | **100%** | Done 2026-04-24 |
| Key protected storage (AM7-L2-SEC-006) | **100%** | Done 2026-04-24 |
| Audit log HMAC (AM7-L2-SEC-003) | **100%** | Done |
| Operator event logging (AM7-L2-LOG-OP) | **100%** | Done 2026-04-24 |
| PISP dual-stream pipeline | **100%** | Done 2026-04-24 (Gemini) |
| ECDSA firmware signing (AM7-L2-SEC-002) | **100%** | Done 2026-04-24 (sign_binary.sh) |
| Gimbal sequence gap detection (AM7-L3-ACT-002) | **100%** | Done 2026-04-24 (freecam callback wired) |

### Overall Completion
- **~60-65% complete** (by functionality)
- **~28% of issues resolved** (by count in report.md)

### Critical Blockers to "Done"
1. **WCET verification on hardware** — PISP dual-stream eliminates software demosaic; need hardware measurement
2. **Detection/tracking tuning** — YOLO26n + KCF pipeline wired, performance not characterized
3. **I2C error handling** — slow-success bug fixed; LRF/detector errors still ~30% resolved

### Is the Product Fully Done?
**No.** Progress this session:
- Security requirements now ~90% complete (was 75%)
- ECDSA signing added: verify_self(), sign_binary.sh, sign_file_ecdsa()
- I2C slow-success bug fixed (was erroneously returning failure on slow-but-valid operations)
- AuroreLink freecam callback wired (was silently dropped)
- Gimbal sequence gap detection wired (AM7-L3-ACT-002)
- 42/42 tests pass
- Still needs: WCET hardware verification, ECDSA signing, I2C hardening