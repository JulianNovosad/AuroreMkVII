# Track 002 Progress Tracking

## Status: Active - BLOCKED BY BUFFER STARVATION

## Date Started: February 9, 2026

## Executive Summary

**Issue:** Only HUD overlay visible, no camera feed  
**Root Cause:** Two issues identified:
1. LockFreeQueue was blocking on mutex (FIXED)
2. ImagePool buffer starvation (IN PROGRESS - BLOCKER)

---

## Phase 1: Queue Blocking Issue (RESOLVED)

### Problem Identified
- LockFreeQueue::pop() used std::mutex which blocked when camera held mutex
- Visualization processor worker thread couldn't acquire mutex to pop frames
- Result: overlaid_video_queue_ was EMPTY

### Solution Applied
- Changed pop() to use try_lock() instead of lock_guard
- Changed wait_pop() to use try_lock() instead of UniqueLock with condition_variable
- Added try_push() and try_push_with_size_check() methods for compatibility

### Files Modified
- `src/lockfree_queue.h` - Non-blocking queue operations
- `src/image_processor.h` - Added running() method, removed is_running_ flag
- `src/image_processor.cpp` - Simplified code, removed start() signal mechanism
- `src/application.cpp` - Removed start() signal calls per MANDATE

### Verification
- Worker thread now enters and can attempt to pop frames
- Queue blocking resolved

---

## Phase 2: Buffer Starvation (IN PROGRESS - BLOCKER)

### Problem Identified
After fixing queue blocking, new error appeared:
```
[ERROR] ImageProcessor: Failed to acquire buffer from pool (Starvation)
[WARNING] ImagePool: Failed to acquire buffer within timeout. Available: 0, In use: 100, Peak: 100
```

### Root Cause Analysis
- ImagePool has 100 buffers
- All 100 buffers are "In use" with 0 available
- Visualization processor cannot acquire buffers to push to overlaid_video_queue_
- This prevents frames from flowing to display

### Evidence
```
[WARNING] ImagePool: Failed to acquire buffer within timeout. Available: 0, In use: 100, Peak: 100
[ERROR] [ERROR] Main Video Stream Failed to acquire metadata buffer from pool.
[ERROR] [ERROR] ImageProcessor: Failed to acquire buffer from pool (Starvation).
```

### Investigation Required
1. Why are buffers not being released back to pool?
2. Where in the pipeline are buffers getting stuck?
3. Is the release happening after display consumes frames?

---

## Technical Notes

### Pipeline Flow (Should Be)
```
Camera → main_video_queue_ → ImageProcessor_Viz → overlaid_video_queue_ → Display
```

### Current State
- Camera captures frames ✓
- main_video_queue_ receives frames ✓
- ImageProcessor_Viz attempts to pop frames ✓
- **BLOCKED:** Cannot acquire buffer from ImagePool
- overlaid_video_queue_ is empty ✗
- Display has no frames to show ✗

---

## Completed Actions
- [x] Display composition code verified working
- [x] GPU shader blending implementation verified correct
- [x] LockFreeQueue made non-blocking
- [x] Removed unnecessary start() signal mechanism
- [x] Worker thread enters and runs
- [x] Queue no longer blocks

---

## Next Actions (Priority Order)
1. **CRITICAL:** Investigate why ImagePool buffers aren't released
   - Check display consumer: Does it release buffers after presenting?
   - Check visualization processor: Does it release input buffers after processing?
2. Fix buffer release path in pipeline
3. Verify camera feed visible on display
4. User confirms visual correctness
5. Mark track as completed

---

## Key Files for Buffer Investigation
- `src/drm_display.cpp` - Display consumer, buffer release after present
- `src/image_processor.cpp` - Buffer acquisition and release
- `src/camera/camera_capture.cpp` - Camera buffer handling
- `src/application.cpp` - Buffer pool configuration
