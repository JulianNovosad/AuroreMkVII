# Keyboard Controls Feature - Specification

## Overview

Add keyboard controls to the aurore-link web frontend for mode switching, gimbal control, and target assignment. This provides native web controls equivalent to the Python viewer (`aurore_link/viewer.py`) functionality.

## Functional Requirements

### 1. Mode Switching
- **Key `1`:** Switch to AUTO mode (sends `mode_switch` command with `mode: "AUTO"`)
- **Key `2`:** Switch to FREECAM mode (sends `mode_switch` command with `mode: "FREECAM"`)

### 2. Gimbal Control (FREECAM mode only)

**Input Sensitivity:**
- **Single tap:** 3° change in respective axis
- **Double tap (within 500ms):** 15° change in respective axis
- **Hold:** 30°/second continuous slew (sends commands at 10Hz with 3° delta each)

**Key Bindings:**
- **Key `W`:** Increase pitch (slew up) - sends `freecam` command
- **Key `S`:** Decrease pitch (slew down) - sends `freecam` command
- **Key `A`:** Decrease yaw (slew left) - sends `freecam` command
- **Key `D`:** Increase yaw (slew right) - sends `freecam` command
- **Key `R`:** Reset gimbal to center (0°, 0°) - sends `freecam` command with az=0, el=0

### 3. Zoom Control
- **Key `+` or `=`:** Zoom in - sends zoom command or updates zoom state
- **Key `-`:** Zoom out - sends zoom command or updates zoom state
- **Mouse wheel up:** Zoom in (same as `+`)
- **Mouse wheel down:** Zoom out (same as `-`)

### 4. Target Assignment (AUTO mode)
- **Left-click on video:** Assign target at click coordinates
  - Convert screen coordinates to camera frame coordinates (1536×864)
  - Send command to set target centroid
- **Right-click on video:** Clear current target
  - Send command to clear tracking

### 5. Visual Feedback
- **Mode change notification:** Display current mode and key hints for 4 seconds after mode switch
- **Position:** Bottom-center or top-center overlay
- **Content:** "MODE: AUTO" or "MODE: FREECAM" with relevant control hints
- **Zoom indicator:** Show current zoom level when changing zoom
- **Fade out:** Smooth opacity transition after 4 seconds

## Non-Functional Requirements

- **Response time:** Key press to command < 50ms
- **No interference:** Keyboard controls must not interfere with existing joystick functionality
- **Context-aware:** WASD controls only active in FREECAM mode
- **Prevent default:** Browser default key actions (scroll, zoom) must be prevented
- **Smooth zoom:** Mouse wheel should have adjustable sensitivity

## Acceptance Criteria

- [ ] Pressing `1` switches to AUTO mode
- [ ] Pressing `2` switches to FREECAM mode
- [ ] WASD keys support tap (3°), double-tap (15°), and hold (30°/sec) inputs
- [ ] R key resets gimbal to center (0°, 0°)
- [ ] `+`/`-` keys zoom in/out
- [ ] Mouse wheel zooms in/out
- [ ] Left-click on video assigns target at click location
- [ ] Right-click on video clears target
- [ ] Mode notification appears for 4 seconds after mode change
- [ ] Zoom notification appears when zoom level changes
- [ ] All controls work with existing WebSocket command interface

## Out of Scope

- Custom key binding configuration
- Gamepad/joystick support
- Mobile touch gestures

## Technical Notes

- Use `keydown` and `keyup` event listeners on `window`
- Track key state for continuous WASD input (send commands while held)
- Implement tap/double-tap timing detection (500ms window)
- Use `wheel` event for mouse zoom with `preventDefault()`
- Use `contextmenu` event to block right-click menu
- Coordinate conversion: screen → canvas → frame (1536×864)
- Hold detection: send commands at 10Hz with 3° delta per command
