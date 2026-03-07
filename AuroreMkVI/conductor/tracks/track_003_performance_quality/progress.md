# Track 003: Performance and Quality Fixes - Progress

## Overall Status: Phase 1 - Diagnosis

## Hardware Confirmed
- **Platform**: Raspberry Pi 5 (4GB RAM)
- **Kernel**: 6.12.62+rpt-rpi-v8 (PREEMPT_RT)
- **Camera**: CSI camera via libcamera (/dev/video0 exists)
- **RAM**: 3.9GB total, ~2.5GB available
- **Swap**: 2GB (minimal 512KB usage during run)

## Phase 1: Diagnosis (Tasks 1-6)
- [ ] Task 1: Profile logic module - PENDING
- [ ] Task 2: Analyze P11ResultToken queue - PENDING
- [ ] Task 3: Measure display rendering FPS - PENDING
- [ ] Task 4: Verify CPU fallback status - PENDING
- [ ] Task 5: Review queue capacities - PENDING
- [x] Task 1.5: Check memory bandwidth - DONE (hardware confirmed)
- [ ] Task 1.6: Verify GPU usage - PENDING

## Phase 2: Performance Fixes
- [ ] Task 6: Fix logic module bottleneck - PENDING (depends on Task 1)
- [ ] Task 6.5: Check logic thread core affinity - PENDING
- [ ] Task 7: Increase queue capacities - PENDING
- [ ] Task 8: Optimize inference - PENDING
- [ ] Task 8.5: Reduce inference input resolution - PENDING
- [ ] Task 9: Verify camera 100 FPS - PENDING (depends on Task 6)
- [ ] Task 10: Fix display 60 FPS - PENDING
- [ ] Task 11: Remove blocking calls - PENDING
- [ ] Task 12: Tune RT priorities - PENDING

## Phase 3: Image Quality
- [ ] Task 13: Adjust camera ISP - PENDING
- [ ] Task 13.5: Check AE/AGC - PENDING
- [ ] Task 14: Reduce shadow darkness - PENDING
- [ ] Task 15: Test lighting conditions - PENDING

## Phase 4: Display Cleanup
- [ ] Task 16: Add display cleanup - PENDING
- [ ] Task 16.5: Add signal handler - PENDING
- [ ] Task 17: Clear framebuffer - PENDING
- [ ] Task 18: Restore terminal - PENDING

## Phase 5: Inference Strict Mode
- [ ] Task 19: Remove CPU fallback - PENDING
- [ ] Task 19.5: Verify model quantization - PENDING
- [ ] Task 20: Add loud failure - PENDING
- [ ] Task 21: Document GPU reqs - PENDING

## Initial Findings from 15-second Run

| Component | Target | Actual | Gap |
|-----------|--------|--------|-----|
| Camera FPS | 100 | 15-18 | -85% |
| Inference IPS | 100 | 14 | -86% |
| Display FPS | 60 | ~4 | -93% |
| Logic CPS | 100 | Unknown | - |

**Root Cause Identified**: P11ResultToken queue overflow (capacity 16)
- Inference produces 14 results/second
- Logic cannot consume fast enough
- Backpressure causes camera slowdown

## Critical Questions Answered

1. **Hardware**: Raspberry Pi 5 ✅
2. **Camera**: CSI via libcamera ✅ (not USB)
3. **Inference Model**: Check model file - TBD
4. **fbdev mmap**: Check if remapping per frame - TBD

## Next Steps
1. Run alternative diagnostics (no perf available)
2. Profile logic module (Task 1)
3. Check GPU utilization (Task 1.6)
