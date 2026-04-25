# Vision WCET Fix + Stream Freeze Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut vision pipeline WCET from 17ms max → <1ms by removing a spurious RGB→BGR copy on camera data that is already in BGR order; fix aurore-link stream freeze when aurore dies.

**Architecture:** The camera ISP is configured for `libcamera::formats::BGR888`, meaning DMA buffer bytes are already in B,G,R order. The current `wrap_as_mat` does `cvtColor(COLOR_RGB2BGR)` — a full-frame pixel copy that (a) costs ~3ms mean/17ms max, (b) incorrectly swaps channels again, inverting the colors. Removing it and returning a zero-copy `cv::Mat` header over the DMA buffer satisfies AM7-L3-VIS-001 and cuts vision latency to <100µs. The stream-freeze fix adds a frame-timeout watchdog in aurore-link that closes browser connections when no MJPEG frame arrives within 3 seconds, so the browser reconnects cleanly.

**Tech Stack:** C++17 (OpenCV, libcamera, NEON), Node.js 18 (aurore-link/server.js)

---

## File Map

| File | Change |
|------|--------|
| `src/drivers/camera_wrapper.cpp` | Remove cvtColor+copy in BGR888 path; return zero-copy Mat header |
| `aurore-link/server.js` | Add frame-arrival watchdog; close stale MJPEG browser connections |
| `src/latency_measurement.cpp` | Re-run to verify improvement (no code change, just measurement) |

---

## Task 1: Zero-copy BGR888 in `wrap_as_mat`

**Files:**
- Modify: `src/drivers/camera_wrapper.cpp:1109-1113`

**Root cause confirmed:** Camera is configured `libcamera::formats::BGR888` (line 673). ISP outputs B,G,R bytes in that order. `wrap_as_mat` currently:
```cpp
cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.plane_data[0]);
impl_->bgr_scratch.create(frame.height, frame.width, CV_8UC3);
cv::cvtColor(rgb, impl_->bgr_scratch, cv::COLOR_RGB2BGR);
return impl_->bgr_scratch;
```
This copies 3.98 MB (1536×864×3) and swaps channels on already-correct BGR data. It is both a spec violation (AM7-L3-VIS-001 forbids memcpy on critical path) and the cause of color inversion.

**Lifetime is safe:** `release_frame(frame)` is at main.cpp:979, after all `bgr_frame` uses. `mjpeg_streamer.push_frame()` calls `copyTo` internally (confirmed in mjpeg_streamer.cpp:100) before returning, so the zero-copy Mat is safe.

- [ ] **Step 1: Read the file and locate the section**

```bash
grep -n "cvtColor\|COLOR_RGB2BGR\|BGR888" src/drivers/camera_wrapper.cpp | head -10
```

Expected output includes line ~1109-1113 with the cvtColor call.

- [ ] **Step 2: Replace the BGR888 path with a zero-copy Mat header**

In `src/drivers/camera_wrapper.cpp`, replace lines 1109–1113:

Old:
```cpp
    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.plane_data[0]);
        impl_->bgr_scratch.create(frame.height, frame.width, CV_8UC3);
        cv::cvtColor(rgb, impl_->bgr_scratch, cv::COLOR_RGB2BGR);
        return impl_->bgr_scratch;
    }
```

New:
```cpp
    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        // ISP configured for BGR888: DMA buffer bytes are already in B,G,R order.
        // Zero-copy: return Mat header over DMA buffer (AM7-L3-VIS-001).
        // Caller MUST NOT use this Mat after camera->release_frame().
        return cv::Mat(frame.height, frame.width, CV_8UC3,
                       frame.plane_data[0], static_cast<size_t>(frame.stride[0]));
    }
```

- [ ] **Step 3: Build and confirm it compiles**

```bash
cmake --build build-native --target aurore -j$(nproc) 2>&1 | tail -5
```

Expected: `[100%] Built target aurore` with no errors.

- [ ] **Step 4: Run the latency measurement tool to verify improvement**

```bash
sudo build-native/aurore_latency_measurement --samples=500 --output=/tmp/latency_after.csv --verbose 2>&1 | grep -A 20 "RESULTS"
```

Expected results (compare against baseline: vision mean=2929µs, max=17009µs):
```
  Vision (wrap_as_mat):
    Min:     < 50 µs
    Max:     < 500 µs
    Mean:    < 100 µs
    Jitter:  < 100 µs
```

If numbers are in this range: Task 1 is done. If vision is still >1ms, there is an unexpected code path — check the format branch taken by inspecting `frame.format` at runtime.

- [ ] **Step 5: Verify color correctness**

Run the aurore binary and view the MJPEG stream in a browser at `http://localhost:8080/stream/mipi`. A white wall or known color target should appear with correct colors (not red-blue swapped). If colors look wrong, the camera may have been reconfigured to RGB888 — check:

```bash
grep -n "RGB888\|BGR888\|pixelFormat" src/drivers/camera_wrapper.cpp | head -5
```

If the format at line 673 is `RGB888`, change the wrap_as_mat fix to:
```cpp
// RGB888: ISP outputs R,G,B — must swap to BGR for OpenCV
cv::Mat rgb(frame.height, frame.width, CV_8UC3,
            frame.plane_data[0], static_cast<size_t>(frame.stride[0]));
impl_->bgr_scratch.create(frame.height, frame.width, CV_8UC3);
cv::cvtColor(rgb, impl_->bgr_scratch, cv::COLOR_RGB2BGR);
return impl_->bgr_scratch;
```
and document the format choice in a comment.

- [ ] **Step 6: Commit**

```bash
git add src/drivers/camera_wrapper.cpp
git commit -m "perf: zero-copy BGR888 path in wrap_as_mat, eliminate cvtColor

Camera ISP outputs BGR888 (B,G,R byte order). The previous path did
cvtColor(RGB2BGR) — a 3.98MB pixel copy that also inverted colors.
Replace with a zero-copy cv::Mat header over the DMA buffer.

Fixes AM7-L3-VIS-001 violation (memcpy on critical path).
Expected: vision stage <100us mean vs previous 2929us mean."
```

---

## Task 2: Safety monitor vision deadline configuration

**Files:**
- Modify: `config/config.json` (or create if missing)

**Context:** `safety_monitor.hpp` triggers `VISION_LATENCY_EXCEEDED` when vision latency > `vision_deadline_ns` (default 10ms). Our previous vision max was 17ms, which tripped this fault. After Task 1, vision is <500µs, so the 10ms default is fine. However, this task tightens the config to catch regressions early.

- [ ] **Step 1: Check if config/config.json exists**

```bash
ls -la config/config.json 2>/dev/null || echo "MISSING"
```

- [ ] **Step 2: If missing, create config/config.json with correct safety deadlines**

Create `config/config.json`:
```json
{
  "camera": {
    "width": 1536,
    "height": 864,
    "fps": 120
  },
  "safety": {
    "vision_deadline_ns": 5000000,
    "actuation_deadline_ns": 16666000,
    "max_consecutive_misses": 3
  }
}
```

`vision_deadline_ns`: 5ms (spec AM7-L2-TIM-002 ≤5ms per stage). After Task 1 fix, mean is <100µs so a 5ms alarm gives 50× headroom while still catching regressions.

- [ ] **Step 3: If config.json already exists, update only the safety section**

Read the file first, then add or update:
```json
"safety": {
  "vision_deadline_ns": 5000000,
  "actuation_deadline_ns": 16666000,
  "max_consecutive_misses": 3
}
```

Do NOT change any other existing keys.

- [ ] **Step 4: Verify config is loaded on startup**

```bash
sudo build-native/aurore --dry-run 2>&1 | grep -i "config\|safety\|deadline" | head -10
```

Expected: no "Failed to load config" warning, and the safety config values should appear in startup output.

- [ ] **Step 5: Commit**

```bash
git add config/config.json
git commit -m "config: set vision_deadline_ns=5ms, matches AM7-L2-TIM-002 WCET spec"
```

---

## Task 3: Aurore-link stream freeze fix

**Files:**
- Modify: `aurore-link/server.js:696-760`

**Context:** When the `aurore` binary dies, the UNIX MJPEG socket closes. `server.js` already handles this — it sets `mipiSocket = null` and schedules a reconnect (line 740–747). But existing browser HTTP connections to `/stream/mipi` are still open and receive no new frames. The browser sees a frozen stream indefinitely. Fix: when the MIPI socket closes, close all current browser MJPEG client connections so they reconnect.

- [ ] **Step 1: Read current broadcastMipiFrame and connectMipiSocket functions**

Read `aurore-link/server.js` lines 705–758 to understand current client tracking.

Current state: `mipiClients` is a `Set` of HTTP response objects. `broadcastMipiFrame` writes to all of them. On socket close/error, only `mipiSocket` is nulled — `mipiClients` are left open.

- [ ] **Step 2: Add frame-arrival watchdog and client disconnect on socket close**

In `aurore-link/server.js`, find and modify the `connectMipiSocket` function's `'close'` and `'error'` handlers. Replace:

```javascript
  mipiSocket.on('error', (err) => {
    console.warn('[MIPI] Socket error:', err.message);
    mipiSocket = null;
    scheduleMipiReconnect();
  });

  mipiSocket.on('close', () => {
    console.log('[MIPI] Socket closed — reconnecting in', MIPI_RECONNECT_MS, 'ms');
    mipiSocket = null;
    scheduleMipiReconnect();
  });
```

With:

```javascript
  function dropMipiClients(reason) {
    console.log(`[MIPI] ${reason} — closing ${mipiClients.size} browser client(s)`);
    for (const r of mipiClients) {
      try { r.end(); } catch {}
    }
    mipiClients.clear();
  }

  mipiSocket.on('error', (err) => {
    console.warn('[MIPI] Socket error:', err.message);
    mipiSocket = null;
    dropMipiClients('socket error');
    scheduleMipiReconnect();
  });

  mipiSocket.on('close', () => {
    console.log('[MIPI] Socket closed — reconnecting in', MIPI_RECONNECT_MS, 'ms');
    mipiSocket = null;
    dropMipiClients('socket closed');
    scheduleMipiReconnect();
  });
```

Note: `dropMipiClients` must be defined INSIDE `connectMipiSocket` (it captures the closure's `mipiClients`). Since `mipiClients` is a module-level `Set`, it's accessible. The function can be defined at module level.

- [ ] **Step 3: Add the same fix for USB socket clients**

Find the USB socket `'error'` and `'close'` handlers (around line 790–805). Apply the same pattern using `usbClients`:

```javascript
  function dropUsbClients(reason) {
    console.log(`[USB] ${reason} — closing ${usbClients.size} browser client(s)`);
    for (const r of usbClients) {
      try { r.end(); } catch {}
    }
    usbClients.clear();
  }
```

Add `dropUsbClients('socket error')` and `dropUsbClients('socket closed')` to the USB error/close handlers.

- [ ] **Step 4: Add a frame-arrival watchdog for the MIPI stream**

After `broadcastMipiFrame` sends a frame, it should reset a watchdog timer. If no frame arrives within 3 seconds (aurore may be hung but not dead), drop clients. Add after `let mipiLatestFrame = null;`:

```javascript
let mipiFrameWatchdog = null;

function resetMipiWatchdog() {
  if (mipiFrameWatchdog) clearTimeout(mipiFrameWatchdog);
  mipiFrameWatchdog = setTimeout(() => {
    console.warn('[MIPI] No frame for 3s — dropping browser clients');
    dropMipiClients('frame timeout');
    mipiFrameWatchdog = null;
  }, 3000);
}
```

Call `resetMipiWatchdog()` at the end of `broadcastMipiFrame`:
```javascript
function broadcastMipiFrame(jpeg) {
  mipiLatestFrame = jpeg;
  const header = `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`;
  for (const r of mipiClients) {
    if (!r.writableEnded) { r.write(header); r.write(jpeg); r.write('\r\n'); }
  }
  resetMipiWatchdog();
}
```

- [ ] **Step 5: Test the fix manually**

```bash
# Terminal 1: start aurore-link
cd /home/pi/AuroreMkVII/aurore-link && node server.js

# Terminal 2: start aurore binary
sudo build-native/aurore

# Open browser to http://localhost:8080/ — confirm stream visible

# Terminal 3: kill aurore
sudo pkill aurore

# Observe in browser: stream should reset/reconnect within 3s (instead of freezing)
# Observe in Terminal 1: should see "[MIPI] socket closed — closing N browser client(s)"
```

- [ ] **Step 6: Commit**

```bash
git add aurore-link/server.js
git commit -m "fix: drop browser MJPEG clients when aurore socket closes or frame times out

Prevents stream freeze when aurore binary dies. Clients now receive an
immediate connection close and reconnect, rather than hanging indefinitely."
```

---

## Self-Review

**Spec coverage:**
- AM7-L2-VIS-003 (≤3ms vision latency): covered by Task 1 — zero-copy brings mean to <100µs
- AM7-L3-VIS-001 (zero-copy, no memcpy on critical path): covered by Task 1
- AM7-L2-TIM-002 (WCET ≤5ms end-to-end): Task 1 removes the dominant contributor
- Issue report blocker #1 (vision WCET >10ms): Task 1
- Issue report blocker #5 (color swap): Task 1 (removing wrong cvtColor)
- Issue report blocker #6 (stream freeze): Task 3

**Not covered (out of scope for this plan):**
- Security requirements (HMAC, ECDSA) — separate plan needed
- YOLO26n ONNX inference — separate plan
- I2C FusionHAT retry logic — separate plan

**Placeholder scan:** No TBD or vague steps. All code shown. Commands include expected output.

**Type consistency:** `cv::Mat` constructor `(rows, cols, type, data, step)` — step is `size_t`, `frame.stride[0]` is `int`, cast applied.
