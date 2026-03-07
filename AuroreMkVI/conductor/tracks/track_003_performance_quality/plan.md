# Track 003: Performance and Quality Fixes - Plan

## Hardware Context (Confirmed)
- **Platform**: Raspberry Pi 5 (4GB)
- **Kernel**: 6.12.62+rpt-rpi-v8 (PREEMPT_RT)
- **Camera**: CSI camera via libcamera (/dev/video0)
- **RAM**: 3.9GB total, ~2.5GB available
- **Swap**: 2GB (minimal usage observed)

## Phase 1: Diagnosis (Tasks 1-6)

- [ ] **Task 1**: Profile logic module to identify consumption bottleneck
  - Location: `src/logic.cpp`
  - Action: Add timing measurements to find slow code path
  - Verification: Log shows which function takes most time

- [ ] **Task 2**: Analyze P11ResultToken queue consumption rate
  - Location: `src/pipeline_structs.h`
  - Action: Check queue capacity and consumption pattern
  - Verification: Queue depth stays low (<5)

- [ ] **Task 3**: Measure actual display rendering FPS
  - Location: `src/fbdev_display.cpp`
  - Action: Add FPS counter and log rendering rate
  - Verification: Display FPS logged every 60 frames

- [ ] **Task 4**: Verify if CPU fallback is still active
  - Location: `src/inference/inference.cpp`
  - Action: Check `use_gpu_detection_` flag value
  - Verification: `use_gpu_detection_ = true` in logs

- [ ] **Task 5**: Review current queue capacities
  - Location: `src/pipeline_structs.h`
  - Action: List all queue capacities and current usage
  - Verification: Document queue sizes

- [ ] **Task 1.5**: Check for memory bandwidth saturation
  - Location: Runtime monitoring (`htop` or `vmstat`)
  - Action: Monitor %mem and swap during 15s test
  - Verification: No swap usage, <80% RAM utilization

- [ ] **Task 1.6**: Verify GPU is actually being used
  - Location: `vcgencmd` or `/sys/kernel/debug/pvr/status`
  - Action: Check GPU load during inference
  - Verification: GPU utilization >50%

## Phase 2: Performance Fixes (Tasks 6-13)

- [ ] **Task 6**: Fix logic module consumption bottleneck
  - Location: `src/logic.cpp`
  - Action: Optimize slow code path identified in Task 1
  - Verification: Logic CPS >= 100

- [ ] **Task 6.5**: Check if logic thread is pinned to wrong core
  - Location: `src/application.cpp` thread affinity setup
  - Action: Verify `sched_setaffinity` calls for logic thread
  - Verification: Logic on Core 3, not competing with camera

- [ ] **Task 7**: Increase queue capacities if needed
  - Location: `src/pipeline_structs.h`
  - Action: Double P11ResultToken capacity from 16 to 32
  - Verification: No overflow warnings

- [ ] **Task 8**: Optimize inference processing
  - Location: `src/inference/inference.cpp`
  - Action: Reduce per-frame processing time
  - Verification: Inference time < 5ms per frame

- [ ] **Task 8.5**: Reduce inference input resolution if needed
  - Location: `src/inference/inference.cpp` preprocessing
  - Action: Check if 300x300→224x224 resize is happening on CPU
  - Verification: Move resize to GPU or eliminate redundant copy

- [ ] **Task 9**: Verify camera runs at 100 FPS
  - **DEPENDS ON**: Task 6 (logic bottleneck must be fixed first)
  - Location: Logs
  - Action: Run binary and check camera FPS
  - Verification: `Camera Frame Rate: 100 FPS` in logs

- [ ] **Task 10**: Fix display rendering to 60 FPS
  - Location: `src/fbdev_display.cpp`
  - Action: Optimize frame rendering (check mmap per frame vs mapped)
  - Verification: Display FPS = 60

- [ ] **Task 11**: Remove blocking calls in pipeline
  - Location: Consumer threads
  - Action: Use non-blocking pop operations
  - Verification: No "WAIT" states in queue accounting

- [ ] **Task 12**: Tune real-time priorities
  - Location: `src/application.cpp`
  - Action: Verify thread priorities match requirements
  - Verification: Camera on Core 1, Logic on Core 3

## Phase 3: Image Quality (Tasks 13-16)

- [ ] **Task 13**: Adjust camera ISP settings for better lighting
  - Location: `src/camera/camera_capture.cpp`
  - Action: Tune brightness, contrast, shadow recovery
  - Verification: Shadows visible, not crushed

- [ ] **Task 13.5**: Check if auto-exposure is fighting you
  - Location: `src/camera/camera_capture.cpp`
  - Action: Lock exposure/gain after initial frame or use manual mode
  - Verification: AE/AGC disabled or constrained in logs

- [ ] **Task 14**: Reduce shadow darkness in image processing
  - Location: `src/image_processor.cpp`
  - Action: Apply shadow recovery algorithm
  - Verification: Test image shows detail in shadows

- [ ] **Task 15**: Test under various lighting conditions
  - Location: Runtime testing
  - Action: Run in different lighting
  - Verification: Consistent quality across conditions

## Phase 4: Display Cleanup (Tasks 16-19)

- [ ] **Task 16**: Add display cleanup on graceful shutdown
  - Location: `src/fbdev_display.cpp`
  - Action: Call cleanup in destructor
  - Verification: Framebuffer cleared on exit

- [ ] **Task 16.5**: Add signal handler for SIGINT/SIGTERM
  - Location: `src/main.cpp`
  - Action: Register cleanup handler before `fbdev_display` init
  - Verification: Ctrl+C triggers cleanup, no hang

- [ ] **Task 17**: Clear framebuffer on exit
  - Location: `src/fbdev_display.cpp`
  - Action: memset to 0 in cleanup
  - Verification: Screen goes black on exit

- [ ] **Task 18**: Restore terminal if needed
  - Location: `src/main.cpp`
  - Action: Check terminal state restoration
  - Verification: Terminal prompt visible after exit

## Phase 5: Inference Strict Mode (Tasks 19-22)

- [ ] **Task 19**: Remove CPU fallback in InferenceEngine
  - Location: `src/inference/inference.cpp`
  - Action: Remove `use_gpu_detection_` fallback logic
  - Verification: No CPU fallback path

- [ ] **Task 19.5**: Verify model is quantized properly for GPU
  - Location: Model file check in `src/inference/`
  - Action: Confirm INT8/FP16 vs FP32 - FP32 will cripple Pi GPU
  - Verification: Model size <10MB, load time <2s

- [ ] **Task 20**: Add loud failure if GPU unavailable
  - Location: `src/inference/inference.cpp`
  - Action: Throw exception with clear error message
  - Verification: Application exits with error

- [ ] **Task 21**: Document GPU requirements
  - Location: `README.md` or `docs/`
  - Action: Add GPU requirement notice
  - Verification: Documentation mentions GPU requirement

## Verification Commands

After implementing all tasks, verify with:
```bash
# Run for 15 seconds
sudo timeout --kill-after=1s 15s /home/pi/Aurore/build/AuroreMkVI

# Check FPS metrics
grep -E "(Camera.*FPS|Inference.*IPS|Logic.*CPS|Display.*FPS)" logs/session_*/run.json

# Check for overflow warnings
grep -c "QUEUE_OVERFLOW_WARNING" logs/session_*/run.json
```

Expected results:
- Camera FPS >= 100
- Inference IPS >= 100
- Logic CPS >= 100
- Display FPS = 60
- Zero QUEUE_OVERFLOW_WARNING

## Dependency Order (Important!)

**Task 9 (verify camera 100 FPS) depends on Task 6 (fix logic) being complete.**

Do NOT verify camera FPS until logic module is consuming at 100+ CPS, otherwise you'll chase backpressure effects, not real camera performance.

## Critical Diagnostics Done

- Hardware: Pi 5, 4GB, PREEMPT_RT kernel ✅
- Camera: CSI via libcamera (/dev/video0 exists) ✅
- Swap: Minimal usage (512KB) ✅
- Perf: Not available - using alternative diagnostics
