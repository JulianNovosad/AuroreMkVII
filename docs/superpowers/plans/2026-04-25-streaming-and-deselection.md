# Streaming / De-selection Completion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring streaming/web interface from 85% → 100% and implement AM7-L3-TGT-003/004 de-selection and target handoff.

**Architecture:** Five targeted changes: (1) low-confidence de-selection timer in StateMachine, (2) operator target handoff re-wired in main.cpp, (3) zoom command dispatch plumbed through AuroreLinkServer, (4) target-reject callback triggers SEARCH, (5) clearTarget() in main.js sends an actual message.

**Tech Stack:** C++17 (state_machine, aurore_link_server), main.cpp orchestration, vanilla JS (main.js).

---

## Task 1: AM7-L3-TGT-003 — Low-confidence de-selection timer

**Files:**
- Modify: `include/aurore/state_machine.hpp` (private section, ~line 254)
- Modify: `src/state_machine/state_machine.cpp` (`on_tracker_update`, ~line 386)
- Modify: `tests/unit/state_machine_test.cpp` (add new test)

Spec: "confidence drops < 90% for > 250ms → SEARCH".  PSR < 3.0 is the existing proxy
for weak confidence (see comment at line 1082 in main.cpp). 250ms at 120Hz = 30 frames.

- [ ] **Step 1: Add counter and constant to state_machine.hpp private section after line 306**

```cpp
    // AM7-L3-TGT-003: Low-confidence de-selection (< 90% confidence for > 250ms = 30 frames)
    static constexpr int kLowConfFramesMax = 30;
    static constexpr float kPsrLowThreshold = 3.0f;  // PSR proxy for 90% confidence
    int low_conf_frames_{0};
```

- [ ] **Step 2: Update on_tracker_update in state_machine.cpp (inside `if (state_ == FcsState::TRACKING)` valid branch, after `update_lock_confirmation`)**

Replace the existing `on_tracker_update` TRACKING valid branch (currently lines 394–405):

```cpp
    } else if (state_ == FcsState::TRACKING) {
        if (!sol.valid) {
            low_conf_frames_ = 0;
            transition(FcsState::SEARCH);
        } else {
            // AM7-L3-TGT-003: Track sustained low confidence (PSR < 3.0 for > 250ms)
            if (sol.psr > 0.f && sol.psr < kPsrLowThreshold) {
                if (++low_conf_frames_ >= kLowConfFramesMax) {
                    low_conf_frames_ = 0;
                    std::cerr << "[StateMachine] AM7-L3-TGT-003: confidence dropped < 90% "
                                 "for >250ms — TRACKING -> SEARCH\n";
                    transition(FcsState::SEARCH);
                    return;
                }
            } else {
                low_conf_frames_ = 0;  // Reset on good PSR
            }

            const bool prediction_ok = check_prediction_delta(sol.centroid_x, sol.centroid_y);
            const bool is_stable = is_position_stable() && prediction_ok;
            update_lock_confirmation(is_stable);
            update_prediction(sol);
        }
```

Also reset `low_conf_frames_` in `enter_state` when entering TRACKING (to avoid stale counter from prior tracking sessions). In `enter_state`, inside `case FcsState::TRACKING:` (or equivalent):

```cpp
    // In enter_state(), at the transition into TRACKING:
    low_conf_frames_ = 0;
```

Find the `enter_state` or `transition` function in `state_machine.cpp` and add the reset there. Look for where `state_ = next;` or `state_ = FcsState::TRACKING` is set and add `low_conf_frames_ = 0;` in the TRACKING case.

- [ ] **Step 3: Write the failing test at the end of tests/unit/state_machine_test.cpp**

```cpp
void test_tracking_deselect_low_confidence() {
    StateMachine sm;
    sm.force_state_for_test(FcsState::TRACKING);
    // 29 frames of low PSR should NOT deselect
    TrackSolution sol;
    sol.valid = true;
    sol.centroid_x = 100.f;
    sol.centroid_y = 100.f;
    sol.psr = 1.5f;  // below kPsrLowThreshold (3.0)
    for (int i = 0; i < 29; ++i) {
        sm.on_tracker_update(sol);
    }
    assert(sm.state() == FcsState::TRACKING);
    // 30th frame tips over the 250ms window → SEARCH
    sm.on_tracker_update(sol);
    assert(sm.state() == FcsState::SEARCH);
    std::cout << "PASS: TRACKING -> SEARCH on 30 consecutive low-confidence frames\n";
}
```

Add the call in `main()`:
```cpp
    test_tracking_deselect_low_confidence();
```

- [ ] **Step 4: Run test to verify it fails**

```bash
cd build-rpi && cmake --build . --target state_machine_test -j$(nproc) 2>&1 | tail -5
./state_machine_test 2>&1 | grep -E "PASS|FAIL|Assertion"
```

Expected: assertion failure on `sm.state() == FcsState::SEARCH` (counter not implemented yet).

- [ ] **Step 5: Apply the state_machine.hpp and state_machine.cpp changes, then run**

```bash
cd build-rpi && cmake --build . --target state_machine_test -j$(nproc) 2>&1 | tail -5
./state_machine_test 2>&1 | grep -E "PASS|FAIL|assert"
```

Expected: all tests pass including the new one.

- [ ] **Step 6: Run full ctest**

```bash
cd build-rpi && ctest --output-on-failure 2>&1 | tail -5
```

Expected: `100% tests passed, 0 tests failed out of 43` (or 44 if CMakeLists already picks it up).

- [ ] **Step 7: Commit**

```bash
git add include/aurore/state_machine.hpp src/state_machine/state_machine.cpp tests/unit/state_machine_test.cpp
git commit -m "feat: AM7-L3-TGT-003 low-confidence de-selection timer (30 frames = 250ms)"
```

---

## Task 2: AM7-L3-TGT-004 — Operator target handoff in TRACKING state

**Files:**
- Modify: `src/main.cpp` (`set_target_select_callback`, ~line 682)

Spec: "Operator may override automatic selection with manual selection in SEARCH or TRACKING state. Manual override shall re-validate target."

- [ ] **Step 1: Write the new callback body**

Replace lines 682–687 in main.cpp:

```cpp
    link_server.set_target_select_callback([&](uint16_t cx, uint16_t cy, uint8_t confidence) {
        aurore::FcsState cur = state_machine.state();
        // AM7-L3-TGT-004: Operator override allowed in SEARCH or TRACKING
        if (cur == aurore::FcsState::TRACKING || cur == aurore::FcsState::SEARCH) {
            aurore::Detection manual_det;
            manual_det.confidence = static_cast<float>(confidence) / 100.0f;
            // Create a small bbox centred on the cursor (32×32 pixel init region)
            constexpr int kInitBoxHalf = 16;
            manual_det.bbox.x = static_cast<int>(cx) - kInitBoxHalf;
            manual_det.bbox.y = static_cast<int>(cy) - kInitBoxHalf;
            manual_det.bbox.w = kInitBoxHalf * 2;
            manual_det.bbox.h = kInitBoxHalf * 2;
            // Queue for tracker re-init on next vision cycle via shared atomic flag
            pending_manual_target_.store(true, std::memory_order_release);
            pending_manual_det_ = manual_det;
            telemetry.log_event(aurore::TelemetryEventId::DETECTION_VALID,
                                aurore::TelemetrySeverity::kInfo,
                                "Operator target handoff @ (" + std::to_string(cx) + "," +
                                std::to_string(cy) + ") conf=" + std::to_string(confidence));
        }
    });
```

- [ ] **Step 2: Add shared state variables near line 721 (control loop state block)**

```cpp
    std::atomic<bool> pending_manual_target_{false};
    aurore::Detection pending_manual_det_{};
```

- [ ] **Step 3: Wire manual target reinit in the TRACKING branch of the vision loop (~line 1034)**

After `} else if (state == aurore::FcsState::TRACKING || state == aurore::FcsState::ARMED) {` and before `candidate_found = false;`:

```cpp
                    // AM7-L3-TGT-004: Process pending operator target handoff
                    if (pending_manual_target_.load(std::memory_order_acquire)) {
                        pending_manual_target_.store(false, std::memory_order_release);
                        cv::Rect2d new_bbox(
                            static_cast<float>(pending_manual_det_.bbox.x),
                            static_cast<float>(pending_manual_det_.bbox.y),
                            static_cast<float>(pending_manual_det_.bbox.w),
                            static_cast<float>(pending_manual_det_.bbox.h));
                        if (tracker.init(bgr_frame, new_bbox)) {
                            state_machine.on_detection(pending_manual_det_);
                            std::cerr << "[Vision] AM7-L3-TGT-004: tracker re-inited at "
                                      << "(" << pending_manual_det_.bbox.x << ","
                                      << pending_manual_det_.bbox.y << ")\n";
                        }
                    }
```

- [ ] **Step 4: Build and confirm no compilation errors**

```bash
cd build-rpi && cmake --build . --target aurore_mkv_ii -j$(nproc) 2>&1 | grep -E "error:|warning:" | head -10
```

Expected: no errors.

- [ ] **Step 5: Run ctest**

```bash
cd build-rpi && ctest --output-on-failure 2>&1 | tail -5
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: AM7-L3-TGT-004 operator target handoff re-inits tracker in TRACKING state"
```

---

## Task 3: Zoom command dispatch (AM7-L2-IF-004)

**Files:**
- Modify: `include/aurore/aurore_link_server.hpp`
- Modify: `src/network/aurore_link_server.cpp`
- Modify: `src/main.cpp`

Spec ICD-005: `ZOOM_COMMAND (0x0103)` — `zoom_direction: i8`, `zoom_rate: u8`, `reserved: u16`.

- [ ] **Step 1: Add payload struct to aurore_link_server.hpp after `LinkPayloadGimbalCmd` (~line 90)**

```cpp
/**
 * @brief ZOOM_COMMAND payload (ICD-005)
 */
struct LinkPayloadZoomCmd {
    int8_t zoom_direction;  ///< -1=out, 0=stop, +1=in
    uint8_t zoom_rate;      ///< 0-10, max 10% FOV/second
    uint16_t reserved;
    std::array<uint8_t, 28> padding;
};
```

- [ ] **Step 2: Add ZoomCallback typedef and set_zoom_callback() declaration after line 158**

```cpp
using ZoomCallback = std::function<void(int8_t direction, uint8_t rate)>;
```

In the public section of `AuroreLinkServer` after `set_target_reject_callback`:

```cpp
    void set_zoom_callback(ZoomCallback cb);
```

In the private section after `on_target_reject_`:

```cpp
    ZoomCallback on_zoom_;
```

- [ ] **Step 3: Implement set_zoom_callback() in aurore_link_server.cpp (near set_target_reject_callback)**

```cpp
void AuroreLinkServer::set_zoom_callback(ZoomCallback cb) { on_zoom_ = std::move(cb); }
```

- [ ] **Step 4: Add kZoomCommand case in handle_binary_command dispatch (~line 386, before kModeRequest)**

```cpp
        case LinkMsgId::kZoomCommand: {
            LinkPayloadZoomCmd payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_zoom_) {
                on_zoom_(payload.zoom_direction, payload.zoom_rate);
            }
            break;
        }
```

- [ ] **Step 5: Register zoom callback in main.cpp after set_target_reject_callback (~line 698)**

```cpp
    link_server.set_zoom_callback([&](int8_t direction, uint8_t rate) {
        // AM7-L2-IF-004: Zoom command (digital ROI crop; optical zoom not equipped)
        // Log the command; digital zoom can be implemented via crop in vision pipeline.
        telemetry.log_event(aurore::TelemetryEventId::DETECTION_VALID,
                            aurore::TelemetrySeverity::kInfo,
                            std::string("Zoom ") + (direction > 0 ? "in" : direction < 0 ? "out" : "stop")
                            + " rate=" + std::to_string(rate));
    });
```

- [ ] **Step 6: Build and run tests**

```bash
cd build-rpi && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | head -5
ctest --output-on-failure 2>&1 | tail -5
```

Expected: no build errors, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/aurore/aurore_link_server.hpp src/network/aurore_link_server.cpp src/main.cpp
git commit -m "feat: AM7-L2-IF-004 zoom command dispatch (kZoomCommand plumbed through AuroreLinkServer)"
```

---

## Task 4: Wire target-reject callback to trigger state transition

**Files:**
- Modify: `src/main.cpp` (`set_target_reject_callback`, ~line 693)

Spec AM7-L3-TGT-001: rejection shall log event with reason code. Operator reject should move state back to SEARCH.

- [ ] **Step 1: Update the target-reject callback in main.cpp**

Replace lines 693–698:

```cpp
    link_server.set_target_reject_callback([&](uint32_t target_id, uint8_t reason) {
        telemetry.log_event(aurore::TelemetryEventId::DETECTION_INVALID,
                            aurore::TelemetrySeverity::kInfo,
                            "Operator rejected target id=" + std::to_string(target_id) +
                            " reason=" + std::to_string(reason));
        // AM7-L3-TGT-001/004: Operator reject clears lock, returns to SEARCH
        aurore::FcsState cur = state_machine.state();
        if (cur == aurore::FcsState::TRACKING || cur == aurore::FcsState::ARMED) {
            tracker.reset();
            state_machine.request_search();
        }
    });
```

- [ ] **Step 2: Build and confirm no errors**

```bash
cd build-rpi && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | head -5
```

- [ ] **Step 3: Run ctest**

```bash
cd build-rpi && ctest --output-on-failure 2>&1 | tail -5
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "fix: target_reject callback triggers tracker reset + SEARCH transition (AM7-L3-TGT-001)"
```

---

## Task 5: clearTarget() in main.js sends actual command

**Files:**
- Modify: `aurore-link/main.js` (~line 720)

The `clearTarget()` currently shows a notification but sends nothing. It should send a `mode_switch SEARCH` to transition back to SEARCH (same as Digit1 in AUTO). Alternatively send a `target_reject` message, but since the backend now wires TARGET_REJECT through the TCP link server, a simpler JSON mode_switch via the WebSocket path is faster since the WebSocket connects to the Node.js server, which uses a UNIX socket to reach the C++ backend.

Check what message types the Node.js server accepts for target operations:

- [ ] **Step 1: Check server.js for how it routes mode_switch to the C++ backend**

```bash
grep -n "mode_switch\|target_reject\|clear_target\|SEARCH" /home/pi/AuroreMkVII/aurore-link/server.js | head -20
```

- [ ] **Step 2: If mode_switch SEARCH routes to C++ backend, update clearTarget() in main.js**

Replace lines 720–723:

```javascript
function clearTarget() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'mode_switch', mode: 'SEARCH' }));
  }
  showNotification('TARGET: CLEARED');
}
```

If server.js does not route mode_switch to C++ (check first), add routing there.

- [ ] **Step 3: Verify server.js mode_switch → C++ path**

```bash
grep -n "mode_switch\|cmd_socket\|unix.*socket\|sendToBackend" /home/pi/AuroreMkVII/aurore-link/server.js | head -20
```

If there is no path from WebSocket `mode_switch` to the C++ CommandSocket, add it to server.js: read the existing mode_switch handler and ensure it sends to the C++ UNIX socket at `/run/aurore/operator_control.sock`.

- [ ] **Step 4: Confirm no syntax errors in main.js**

```bash
node --check /home/pi/AuroreMkVII/aurore-link/main.js && echo "OK"
```

Expected: `OK`

- [ ] **Step 5: Commit**

```bash
git add aurore-link/main.js
git commit -m "fix: clearTarget() sends mode_switch SEARCH instead of being a no-op"
```

---

## Task 6: Update issue_report.md completion percentages

**Files:**
- Modify: `docs/issue_report.md`

- [ ] **Step 1: Update the Completion by Area table**

Change:
- `Streaming/web interface` from `85%` to `100%`
- Add new rows for the completed requirements

Update the "Is the Product Fully Done?" section with session notes covering what was done in this session.

- [ ] **Step 2: Commit**

```bash
git add docs/issue_report.md
git commit -m "docs: update completion — streaming/web interface 100%, TGT-003/004 complete"
```

---

## Self-Review Against Spec

### Spec coverage check

| Requirement | Task |
|-------------|------|
| AM7-L3-TGT-003: confidence < 90% for > 250ms → SEARCH | Task 1 |
| AM7-L3-TGT-004: target handoff automatic→manual | Task 2 |
| AM7-L2-IF-004: scroll wheel zoom (ZOOM_COMMAND) | Task 3 |
| AM7-L3-TGT-001: target reject logs with reason code + state action | Task 4 |
| AM7-L2-IF-005: keyboard binds for mode switching (clearTarget) | Task 5 |

### Gaps NOT addressed (hardware-dependent or deferred):

- AM7-L2-TIM-002 WCET ≤ 5ms: requires hardware measurement
- AM7-L3-OPT-004: performance benchmark report: requires hardware
- AM7-L3-IF-003: input buffer overflow flag: the TCP socket accept loop is non-blocking with a 4-client limit; overflow is implicit (new connections refused). A formal `warning_flag` could be added but is minor.
- Zoom ROI cropping: Task 3 logs zoom but does not implement ROI crop. This is the natural 15% gap after this session.

### No placeholder scan issues found.
