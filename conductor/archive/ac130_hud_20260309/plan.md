# AC-130 HUD Frontend Redesign - Plan

## Phase 1: Project Scaffolding

### 1.1 Create Conductor Directory Structure
- [ ] Create `conductor/` directory
- [ ] Initialize `setup_state.json`
- [ ] Create `tracks/` directory

### 1.2 Create Product Documentation
- [ ] Write `product.md` (product vision, goals, features)
- [ ] Write `product-guidelines.md` (style, tone, UI/UX)
- [ ] Write `tech-stack.md` (document inferred stack)

### 1.3 Create Code Style Guides
- [ ] Write `code_styleguides/cpp.md` (C++17 style)
- [ ] Write `code_styleguides/javascript.md` (ES2022+ style)

### 1.4 Create Workflow Document
- [ ] Write `workflow.md` (task management, git, testing)

### 1.5 Generate Initial Track
- [ ] Create `tracks.md` registry
- [ ] Create track directory `ac130_hud_20260309/`
- [ ] Write `metadata.json`
- [ ] Write `spec.md`
- [ ] Write `plan.md`

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 1: Project Scaffolding' (Protocol in workflow.md)

---

## Phase 2: HTML Structure Redesign

### 2.1 Update index.html
- [ ] Add Share Tech Mono font from Google Fonts
- [ ] Remove sidebar layout structure
- [ ] Add full-screen HUD overlay grid
- [ ] Add quadrant divs (top-left, top-center, top-right, etc.)
- [ ] Add main reticle SVG (full-screen stadia)
- [ ] Add ballistic pipper SVG (offset crosshair)
- [ ] Add corner bracket SVGs (4 L-shaped corners)
- [ ] Add analog dial SVG (gimbal yaw indicator)
- [ ] Add horizontal scale bar SVG (mil-ticks)
- [ ] Make joystick invisible (opacity: 0)

### 2.2 Verify HTML Structure
- [ ] Validate HTML syntax
- [ ] Check all element IDs match JS references
- [ ] Verify SVG viewBox attributes

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 2: HTML Structure Redesign' (Protocol in workflow.md)

---

## Phase 3: CSS Styling

### 3.1 Rewrite style.css
- [ ] Define monochrome CSS variables (--ac130-white, --ac130-dim)
- [ ] Set Share Tech Mono font globally
- [ ] Add font aliasing rules (-webkit-font-smoothing: none)
- [ ] Create CSS Grid layout (280px 1fr 280px columns)
- [ ] Style quadrant positioning
- [ ] Style telemetry text (no borders, no backgrounds)
- [ ] Add FAULT blink animation (3s cycle)
- [ ] Style analog dial container
- [ ] Style main reticle positioning
- [ ] Style pipper positioning
- [ ] Style corner brackets (L-shape with ::before/::after)
- [ ] Add scanline overlay (video layer only)
- [ ] Make joystick invisible but functional
- [ ] Remove all CSS transitions

### 3.2 Verify CSS
- [ ] Check no color values except white/gray
- [ ] Verify no transition properties
- [ ] Test responsive layout (phone view)

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 3: CSS Styling' (Protocol in workflow.md)

---

## Phase 4: JavaScript Implementation

### 4.1 Update main.js DOM References
- [ ] Update getElementById calls for new element IDs
- [ ] Add references for scale bar elements
- [ ] Add references for bracket elements
- [ ] Add reference for gimbal needle

### 4.2 Implement HUD Update Functions
- [ ] updateFcsState() - raw text "SYS: ARMED"
- [ ] updatePipper() - position based on ballistic.az/el_lead_mrad
- [ ] updateTrackBrackets() - position 4 corners around target
- [ ] updateGimbalDial() - rotate needle based on yaw
- [ ] updateScaleBar() - update range/mil-scale display
- [ ] Remove color-coded state logic
- [ ] Remove P_hit bar animation

### 4.3 Implement FAULT Blink
- [ ] Add setInterval for 3s blink cycle
- [ ] Toggle opacity 100% ↔ 30%
- [ ] Reset opacity on state change

### 4.4 Preserve WebSocket Logic
- [ ] Keep connection logic unchanged
- [ ] Keep JSON parsing unchanged
- [ ] Keep command sending unchanged
- [ ] Keep auto-reconnect logic

### 4.5 Verify JavaScript
- [ ] Run eslint (zero errors)
- [ ] Test WebSocket connection
- [ ] Verify all telemetry updates correctly

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 4: JavaScript Implementation' (Protocol in workflow.md)

---

## Phase 5: Testing and Verification

### 5.1 Manual Testing
- [ ] Start mock server
- [ ] Open browser (hard refresh)
- [ ] Verify WebSocket connects
- [ ] Verify all telemetry displays
- [ ] Verify reticle renders correctly
- [ ] Verify corner brackets appear on target
- [ ] Verify pipper moves with ballistic data
- [ ] Verify analog dial rotates with yaw
- [ ] Verify FAULT blinks (simulate FAULT state)
- [ ] Verify joystick controls gimbal

### 5.2 Browser Testing
- [ ] Test on Chrome
- [ ] Test on Firefox
- [ ] Verify cache busting works

### 5.3 Code Quality
- [ ] Run eslint (zero errors)
- [ ] Check console (zero warnings)
- [ ] Verify no color values in CSS
- [ ] Verify no transitions in CSS

### 5.4 Documentation
- [ ] Update tracks.md status
- [ ] Update metadata.json updated_at
- [ ] Document any deviations from spec

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 5: Testing and Verification' (Protocol in workflow.md)

---

## Phase 6: Finalization

### 6.1 Commit Changes
- [ ] git add all conductor/ files
- [ ] git add aurore-link/ changes
- [ ] Commit with conventional message
- [ ] Add git note with track summary

### 6.2 Update Track Status
- [ ] Mark track as completed in tracks.md
- [ ] Update metadata.json status to "completed"
- [ ] Update updated_at timestamp

### 6.3 Retrospective
- [ ] Document lessons learned
- [ ] Note any technical debt
- [ ] Suggest follow-up tracks

**Phase Completion Task:**
- [ ] Task: Conductor - User Manual Verification 'Phase 6: Finalization' (Protocol in workflow.md)

---

## Task Estimates

| Phase | Estimated Tasks | Complexity |
|-------|----------------|------------|
| Phase 1 | 10 | Low |
| Phase 2 | 12 | Medium |
| Phase 3 | 15 | Medium |
| Phase 4 | 12 | Medium |
| Phase 5 | 12 | Low |
| Phase 6 | 6 | Low |

**Total:** ~67 tasks
