# Track 002: HUD Overlay and Camera Feed Blending Issue

## Objective

Fix the display issue where only the HUD overlay is visible but the camera feed is not showing. The problem appears to be that one buffer is overwriting the other instead of properly blending the camera feed with the HUD overlay. The solution must be hardware accelerated using GPU capabilities, not CPU-based decorations.

## Problem Statement

Current display behavior:
- Only HUD overlay is visible on screen
- Camera feed is not displayed
- Suspected cause: Buffer overwriting instead of alpha blending
- Expected: Camera feed as background with HUD overlay composited on top

## Success Criteria

- Camera feed is visible as background
- HUD overlay is properly composited on top of camera feed
- Both elements are visible and readable
- Proper alpha blending between camera feed and HUD
- No flickering or visual artifacts
- Solution is hardware accelerated using GPU (OpenGL ES) - NO CPU decorations