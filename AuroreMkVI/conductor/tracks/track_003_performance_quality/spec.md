# Track 003: Performance and Quality Fixes

## Overview
Fix camera FPS to target 100 FPS, improve image lighting (reduce dark shadows), return display to Linux terminal on exit, and make inference fail loudly without CPU fallback.

## Requirements

### 1. Camera Performance
- **Target FPS**: 100 FPS (not 120 FPS as previously)
- **Current State**: ~15-18 FPS due to P11ResultToken queue overflow
- **Root Cause**: Logic module consuming too slowly, causing backpressure

### 2. Image Quality (Lighting)
- **Current Issue**: Shadows are too dark, lighting is poor
- **Required**: Improve camera image processing to reduce shadows and improve visibility
- **Target**: Balanced lighting across the frame

### 3. Display Cleanup
- **Current Issue**: Display not returned to Linux terminal on exit
- **Required**: Clean display restoration on shutdown (clear framebuffer, restore terminal)

### 4. Inference Behavior Change
- **Current State**: CPU fallback enabled when GPU unavailable
- **Required**: Fail loudly if GPU not available, disable CPU fallback
- **Behavior**: Throw exception or exit with error if EGL/GPU initialization fails

### 5. Logic Module Optimization
- **Current State**: Queue overflow on P11ResultToken (capacity 16)
- **Required**: Consume at 100+ FPS to match inference rate
- **Target**: Zero overflow warnings, stable pipeline

## Target System Metrics

| Component | Target | Current | Status |
|-----------|--------|---------|--------|
| Camera | 100 FPS | 15-18 FPS | ❌ Below target |
| Inference | 100 IPS | 14 IPS | ❌ Below target |
| Logic | 100 CPS | Unknown | ❌ Unknown |
| Display | 60 FPS | ~3 FPS | ❌ Below target |

## Technical Details

### Camera Bottleneck Analysis
The P11ResultToken queue overflow indicates:
1. Inference produces ~14 results/second
2. Logic module cannot consume fast enough
3. Result tokens back up and block camera production
4. Camera slows to match slowest consumer (Worst-Case Execution Time violation)

### Display Rendering Issue
- Current: Frames rendered at ~3 FPS (60 frames in 15 seconds = 4 FPS)
- Issue: FbdevDisplay consumer not keeping up with camera frame rate
- Target: 60 FPS display rendering

## Implementation Scope

### Phase 1: Diagnosis
- [ ] Profile logic module to find bottleneck
- [ ] Analyze P11ResultToken queue consumption rate
- [ ] Measure actual display rendering FPS
- [ ] Identify if CPU fallback is still active

### Phase 2: Performance Fixes
- [ ] Fix logic module consumption (target: 100+ CPS)
- [ ] Increase queue capacities if needed
- [ ] Optimize inference processing
- [ ] Verify camera runs at 100 FPS

### Phase 3: Image Quality
- [ ] Adjust camera ISP settings for better lighting
- [ ] Reduce shadow darkness in image processing
- [ ] Test under various lighting conditions

### Phase 4: Display Cleanup
- [ ] Add display cleanup on graceful shutdown
- [ ] Clear framebuffer on exit
- [ ] Restore terminal if needed

### Phase 5: Inference Strict Mode
- [ ] Remove CPU fallback in InferenceEngine
- [ ] Add loud failure (exit with error) if GPU unavailable
- [ ] Document GPU requirements clearly

## Dependencies
- libcamera for camera configuration
- OpenCV for image processing adjustments
- FbdevDisplay module for display cleanup
- InferenceEngine for GPU requirement enforcement

## Success Criteria
- [ ] Camera: 100 FPS sustained
- [ ] Inference: 100 IPS (GPU mode)
- [ ] Logic: 100 CPS with zero queue overflow
- [ ] Display: 60 FPS rendering
- [ ] Lighting: Balanced shadows, improved visibility
- [ ] Display: Clean return to terminal on exit
- [ ] Inference: Loud failure if GPU unavailable
