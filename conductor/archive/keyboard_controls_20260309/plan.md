# Keyboard Controls Feature - Implementation Plan

## Phase 1: Keyboard Input Handling

### 1.1 Add Event Listeners
- [ ] Add `keydown` event listener on `window`
- [ ] Add `keyup` event listener on `window`
- [ ] Add `wheel` event listener on canvas
- [ ] Add `contextmenu` event listener to block right-click menu
- [ ] Implement `preventDefault()` for all control keys

### 1.2 Key State Tracking
- [ ] Create key state object to track held keys
- [ ] Implement key state update on keydown/keyup
- [ ] Add game loop interval for continuous input (10Hz command rate)
- [ ] Implement tap/double-tap timing detection (500ms window)

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 1: Keyboard Input Handling' (Protocol in workflow.md)

---

## Phase 2: Mode Switching Implementation

### 2.1 Mode Switch Commands
- [ ] Implement handler for key `1` → send `mode_switch` with `mode: "AUTO"`
- [ ] Implement handler for key `2` → send `mode_switch` with `mode: "FREECAM"`
- [ ] Add visual notification display (4 second fade)
- [ ] Block mode switching when not in valid state

### 2.2 Visual Feedback
- [ ] Create notification overlay element in HTML
- [ ] Implement `showNotification(message, duration)` function
- [ ] Add CSS for notification styling (bottom-center, 4s fade)
- [ ] Test notification timing and opacity transition

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 2: Mode Switching Implementation' (Protocol in workflow.md)

---

## Phase 3: Gimbal Control Implementation

### 3.1 WASD Rate Control
- [ ] Implement tap detection (single press → 3° delta)
- [ ] Implement double-tap detection (within 500ms → 15° delta)
- [ ] Implement hold detection (continuous 30°/second slew)
- [ ] Track key press timing and count for each WASD key
- [ ] W key → positive pitch delta
- [ ] S key → negative pitch delta
- [ ] A key → negative yaw delta
- [ ] D key → positive yaw delta
- [ ] Only active in FREECAM mode
- [ ] Send `freecam` commands at 10Hz while holding

### 3.2 Reset Function
- [ ] Implement R key → send `freecam` with az=0, el=0
- [ ] Add visual confirmation for reset action

### 3.3 Zoom Control
- [ ] Implement `+`/`=` keys → zoom in command
- [ ] Implement `-` key → zoom out command
- [ ] Implement mouse wheel → zoom in/out
- [ ] Add zoom level notification display
- [ ] Adjust wheel sensitivity for smooth zoom

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Gimbal Control Implementation' (Protocol in workflow.md)

---

## Phase 4: Target Assignment Implementation

### 4.1 Click-to-Target
- [ ] Add `mousedown` event listener on canvas
- [ ] Implement left-click handler: convert screen → canvas → frame coordinates
- [ ] Send target assignment command with centroid coordinates
- [ ] Add visual feedback for target assignment

### 4.2 Clear Target
- [ ] Implement right-click handler: clear current target
- [ ] Send clear target command
- [ ] Add visual confirmation for target clear

### 4.3 Coordinate Conversion
- [ ] Implement `screenToFrame(screenX, screenY)` function
- [ ] Handle canvas scaling (display size → 1536×864)
- [ ] Handle aspect ratio and letterboxing
- [ ] Test coordinate accuracy across screen

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 4: Target Assignment Implementation' (Protocol in workflow.md)

---

## Phase 5: Testing and Verification

### 5.1 Manual Testing
- [ ] Test mode switching with `1` and `2` keys
- [ ] Test WASD controls (tap 3°, double-tap 15°, hold 30°/sec)
- [ ] Test R key reset function
- [ ] Test zoom with `+`/`-` and mouse wheel
- [ ] Test left-click target assignment
- [ ] Test right-click target clear
- [ ] Test notification timing (4s fade)
- [ ] Test that controls don't interfere with joystick

### 5.2 Browser Testing
- [ ] Test on Chrome/Chromium
- [ ] Test on Firefox
- [ ] Verify `preventDefault()` blocks browser defaults
- [ ] Verify no console errors

### 5.3 Code Quality
- [ ] Run `npm run lint` (zero errors)
- [ ] Verify code formatting
- [ ] Add JSDoc comments for new functions

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 5: Testing and Verification' (Protocol in workflow.md)

---

## Phase 6: Finalization

### 6.1 Documentation
- [ ] Update README.md with keyboard controls documentation
- [ ] Add keyboard controls section to aurore-link/CLAUDE.md
- [ ] Document key bindings in comments

### 6.2 Commit
- [ ] git add all changes
- [ ] Commit with conventional message: `feat(aurore-link): add keyboard controls for mode, gimbal, and targeting`
- [ ] Add git note with track summary

### 6.3 Track Completion
- [ ] Update track status to `[x]` in tracks.md
- [ ] Update metadata.json status to "completed"
- [ ] Update updated_at timestamp

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 6: Finalization' (Protocol in workflow.md)
