# Calibration Web Interface — Design Spec
**Date:** 2026-03-31
**Project:** Aurore MkVII
**Scope:** Web-based calibration interface integrated into the existing `aurore-link` server

---

## 1. Overview

Add a calibration page at `/calibrate` to the existing `aurore-link` Node.js server. The page provides:
- Live dual-camera MJPEG feeds with crosshair overlays
- Servo centering and jog controls
- LRF live distance feed
- Three-step calibration workflow
- Calibration data saved to `config/calibration.json`

The main C++ binary is **stopped** during calibration. Node.js drives all hardware directly.

No mocks, no simulated data, no fallback images. Hardware absence returns an HTTP error.

---

## 2. Architecture & File Layout

`mock-server.js` is renamed to `server.js` (it is no longer a mock).

### New files
```
aurore-link/
  calibrate.html     — calibration SPA shell
  calibrate.js       — page logic (WebSocket, servo jog, step workflow, save)
  calibrate.css      — styles (reuses :root CSS variables from style.css)
```

### New server endpoints (added to server.js)
```
GET  /calibrate            → serve calibrate.html
GET  /stream/mipi          → MJPEG stream from libcamera-vid subprocess
GET  /stream/usb           → MJPEG stream from ffmpeg subprocess
POST /api/servo/center     → write both channels to 90°
POST /api/servo/angle      → {pan_deg, tilt_deg} absolute, clamped 0–180°
GET  /ws/calib             → WebSocket: live LRF mm + servo angles at ~10Hz
POST /api/calibration/save → write config/calibration.json
```

### Calibration output
`config/calibration.json` at the repo root (directory created if absent).

---

## 3. Camera Streaming

Both streams: `multipart/x-mixed-replace` MJPEG, displayed via `<img src="...">`.

**MIPI CSI-2** (`/stream/mipi`) — 1536×864 @ 60fps:
```
libcamera-vid --codec mjpeg --width 1536 --height 864 --framerate 60 --nopreview -o -
```

**USB webcam** (`/stream/usb`) — 1280×720 @ 60fps:
```
ffmpeg -f v4l2 -input_format mjpeg -framerate 60 -video_size 1280x720 -i /dev/video0 -f mjpeg pipe:1
```

USB device `/dev/video*` is probed at request time for `uvcvideo` driver (same logic as `usb_camera.cpp`). Returns `503` if camera not found — no placeholder.

Frame envelope:
```
--frame\r\n
Content-Type: image/jpeg\r\n
\r\n
<jpeg bytes>\r\n
```

Crosshair overlay: SVG element absolutely positioned over each `<img>`. Always visible. Turns green when user clicks "Mark Aligned" in Step 2.

---

## 4. Servo Control

**Hardware:** FusionHat sysfs interface.
**Channels:** pan = 10, tilt = 11.
**Sysfs paths:**
```
/sys/class/fusion_hat/fusion_hat/pwm10/duty_cycle   ← pan
/sys/class/fusion_hat/fusion_hat/pwm11/duty_cycle   ← tilt
```

**Angle → duty cycle (50Hz, period = 20,000,000 ns):**
```
pulse_us      = 1000 + (angle_deg / 180) * 1000
duty_cycle_ns = pulse_us * 1000
```

Center (90°) = 1,500,000 ns.

**Endpoints:**
- `POST /api/servo/center` — writes both channels to 90°, no body
- `POST /api/servo/angle` — body `{pan_deg, tilt_deg}`, server-side clamped silently to 0–180° before writing

**Sysfs `enable` file** written `1` before first move, left enabled for the session.

**Jog controls** (client-side, sends `/api/servo/angle`): ±1°, ±5°, ±10° for pan and tilt independently. Current angles tracked in browser state, updated from WebSocket.

---

## 5. LRF Live Feed

**WebSocket `/ws/calib`** pushes JSON at ~10Hz:
```json
{"lrf_mm": 985, "pan_deg": 90.0, "tilt_deg": 90.0, "ts": 1743000000000}
```

**UART setup:** `stty` configures `/dev/ttyAMA0` at 9600 baud raw before `fs.open`. Bytes accumulated in a buffer and parsed for M01 13-byte frames:
- Sync byte `0xAA`, bytes[8:9] as 4-nibble BCD in mm
- Checksum: `sum(bytes[1..11]) & 0xFF === bytes[12]`
- Only valid, checksum-passing frames emit a distance reading

**Re-stimulation:** continuous command `AA 00 00 21 00 01 00 00 22` re-sent every 1.5s of silence (matches C++ driver `kMaxIdlePolls` behaviour).

**Toggle:** browser sends `{"type":"lrf_start"}` / `{"type":"lrf_stop"}` over the WebSocket to open/close the UART fd.

**Error:** if `/dev/ttyAMA0` cannot be opened, server sends `{"error":"lrf_unavailable"}`. No retry loop. UI shows static error label.

---

## 6. Calibration Workflow UI

### Layout
```
┌─────────────────────────────────────────────┐
│  ← Back to HUD          CALIBRATION MODE    │
├──────────────────────┬──────────────────────┤
│  MIPI feed           │  USB feed            │
│  [crosshair SVG]     │  [crosshair SVG]     │
│  LRF: 985mm          │                      │
├──────────────────────┴──────────────────────┤
│  [Step 1] [Step 2] [Step 3]                 │
│  ─── Step controls ───────────────────────  │
├─────────────────────────────────────────────┤
│  [ Save All Calibration Data ]  Status bar  │
└─────────────────────────────────────────────┘
```

Steps unlock sequentially (Step 2 unlocks after Step 1 done, Step 3 after Step 2).

### Step 1 — Center Servos
- "Center" button → `POST /api/servo/center`
- Jog grid: Pan row (±1°, ±5°, ±10°), Tilt row (±1°, ±5°, ±10°)
- Current angles displayed live from WebSocket
- "Done — servos centred" button marks step complete, unlocks Step 2

### Step 2 — LRF Alignment
- "Activate LRF" toggle → `lrf_start` / `lrf_stop` over WebSocket
- Live distance badge shown above both camera feeds
- Servo jog controls remain active
- "Mark Aligned" button records `{pan_deg, tilt_deg, lrf_mm}`, crosshairs turn green
- "Save Centre Offset" stores data in memory, unlocks Step 3

### Step 3 — Camera-to-Camera Offset
- User clicks one point on MIPI feed, one corresponding point on USB feed
- Pixel offset `(dx, dy)` computed and displayed
- Multiple pairs can be added and averaged
- "Save Camera Offset" stores averaged offset in memory

### Save bar
- "Save All Calibration Data" enabled after Steps 1 and 2 complete
- `POST /api/calibration/save`
- Shows saved file path and timestamp on success

---

## 7. Calibration Data Format

File: `config/calibration.json` (overwritten on each save, no versioning).

```json
{
  "saved_at": "2026-03-31T12:00:00.000Z",
  "servo": {
    "pan_channel": 10,
    "tilt_channel": 11,
    "pan_center_deg": 92.0,
    "tilt_center_deg": 88.0
  },
  "lrf": {
    "pan_at_center_deg": 92.0,
    "tilt_at_center_deg": 88.0,
    "distance_at_center_mm": 985
  },
  "cameras": {
    "pixel_offset_x": 14,
    "pixel_offset_y": -3,
    "sample_count": 3
  }
}
```

`cameras` block is `null` if Step 3 was not completed. Save is permitted after Steps 1 and 2 only.

---

## 8. Constraints

- **No mocks.** Hardware absence → HTTP error, not simulated data.
- **Main binary stopped** during calibration — Node.js owns UART, sysfs, cameras directly.
- **No new npm dependencies** beyond what `server.js` already uses (`ws` package). UART handled via Node.js `fs` + `child_process` for `stty`. ffmpeg and libcamera-vid must be present on the system.
- **Vanilla JS/CSS** — no framework, no bundler, consistent with existing aurore-link style.
- **Responsive** — layout works on desktop and tablet (≥768px wide).
