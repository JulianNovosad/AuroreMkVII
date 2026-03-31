# Calibration Web Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `/calibrate` page to the aurore-link server that enables servo centering, LRF alignment, and camera-to-camera offset calibration via web browser — no SSH required.

**Architecture:** Extend `aurore-link/server.js` (renamed from `mock-server.js`) with MJPEG camera stream endpoints, sysfs servo control, a `/ws/calib` WebSocket for live LRF + servo state, and a calibration save endpoint. Three new files (`calibrate.html`, `calibrate.js`, `calibrate.css`) implement the browser UI.

**Tech Stack:** Node.js 18+, vanilla JS/CSS, `ws` npm package (already installed), `libcamera-vid` for MIPI MJPEG, `ffmpeg` for USB MJPEG, Linux sysfs for servo PWM, Node.js `net.Socket` over raw fd for UART LRF.

---

## Spec corrections (discovered during planning)

Two errors in the design spec — use the corrected values below:

| Field | Spec (wrong) | Actual |
|-------|-------------|--------|
| Sysfs path | `/sys/class/fusion_hat/fusion_hat/pwm10/` | `/sys/class/fusion_hat/fusion_hat/pwm/pwm10/` |
| Duty cycle units | nanoseconds | **microseconds** (1000–2000 μs) |

The C++ driver writes `int period_us = 1000000 / 50 = 20000` and duty_cycle in the same units (μs). Confirmed via `get_pwm_path()` in `fusion_hat.cpp:705–706`.

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `aurore-link/mock-server.js` | Rename → `server.js` | HTTP + WebSocket server |
| `aurore-link/server.js` | Extend | Add all new endpoints + WS routing |
| `aurore-link/package.json` | Modify | Update `main` and `start` script |
| `aurore-link/calibrate.html` | Create | Calibration page shell: layout, camera `<img>`, step tabs |
| `aurore-link/calibrate.css` | Create | Page styles (reuses `:root` CSS vars from style.css) |
| `aurore-link/calibrate.js` | Create | WebSocket client, servo jog, step workflow, save |
| `aurore-link/CLAUDE.md` | Modify | Update file map table |

---

## Task 1: Rename server + add /calibrate route

**Files:**
- Rename: `aurore-link/mock-server.js` → `aurore-link/server.js`
- Modify: `aurore-link/package.json`
- Modify: `aurore-link/server.js` (add /calibrate route + WS routing by URL)
- Create: `aurore-link/calibrate.html` (minimal skeleton)

- [ ] **Step 1: Rename the file**

```bash
cd /home/pi/AuroreMkVII/aurore-link
git mv mock-server.js server.js
```

- [ ] **Step 2: Update package.json**

Replace the `"main"` and `"start"` fields:

```json
{
  "name": "aurore-link",
  "version": "1.0.0",
  "description": "Aurore MkVII web remote control station",
  "main": "server.js",
  "scripts": {
    "start": "node server.js",
    "lint": "eslint .",
    "lint:fix": "eslint . --fix"
  },
  "dependencies": {
    "ws": "^8.0.0"
  },
  "engines": {
    "node": ">=18"
  },
  "license": "UNLICENSED",
  "devDependencies": {
    "@eslint/js": "^10.0.1",
    "eslint": "^10.0.3",
    "eslint-plugin-security": "^4.0.0",
    "globals": "^17.4.0"
  }
}
```

- [ ] **Step 3: Add /calibrate static route and split WebSocket routing by URL**

In `server.js`, find the existing `new WebSocketServer({ server })` line and the existing `wss.on('connection', ...)` block. Replace the single WebSocketServer setup with two servers routed by URL. The existing HUD logic stays unchanged — only the instantiation changes.

Find this pattern (near the bottom of the file, after `const server = ...`):

```js
const wss = new WebSocketServer({ server });
```

Replace it with:

```js
const hudWss  = new WebSocketServer({ noServer: true });
const calibWss = new WebSocketServer({ noServer: true });

server.on('upgrade', (req, socket, head) => {
  if (req.url === '/ws/calib') {
    calibWss.handleUpgrade(req, socket, head, (ws) => calibWss.emit('connection', ws, req));
  } else {
    hudWss.handleUpgrade(req, socket, head, (ws) => hudWss.emit('connection', ws, req));
  }
});
```

Then find every occurrence of `wss.on(` or `wss.clients` in the file and rename `wss` → `hudWss`.

Also add the /calibrate static route inside `serveStatic` by adding a path alias **before** the `fs.readFile` call:

```js
function serveStatic(req, res) {
  let urlPath = req.url === '/' ? '/index.html' : req.url;
  if (urlPath === '/calibrate') urlPath = '/calibrate.html';  // ADD THIS LINE
  urlPath = urlPath.split('?')[0];
  // ... rest unchanged
```

- [ ] **Step 4: Create minimal calibrate.html skeleton**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Aurore MkVII — Calibration</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="style.css">
  <link rel="stylesheet" href="calibrate.css">
</head>
<body>
  <div id="cal-app">
    <header id="cal-header">
      <a href="/" id="back-link">← HUD</a>
      <span id="cal-title">CALIBRATION MODE</span>
      <span id="cal-status"></span>
    </header>
    <p id="cal-placeholder" style="color:#fff;padding:2rem;">Loading calibration interface...</p>
  </div>
  <script src="calibrate.js"></script>
</body>
</html>
```

- [ ] **Step 5: Create minimal calibrate.css**

```css
/* Calibration page — extends style.css :root variables */

#cal-app {
  min-height: 100vh;
  background: var(--bg);
  color: var(--ac130-white);
  font-family: 'Share Tech Mono', monospace;
  display: flex;
  flex-direction: column;
  overflow-y: auto;
}

#cal-header {
  display: flex;
  align-items: center;
  gap: 1.5rem;
  padding: 0.75rem 1.5rem;
  border-bottom: 1px solid rgba(255,255,255,0.2);
  background: #111;
}

#back-link { color: var(--ac130-dim); text-decoration: none; }
#back-link:hover { color: var(--ac130-white); }
#cal-title { flex: 1; text-align: center; letter-spacing: 0.2em; }
#cal-status { font-size: 0.75rem; color: var(--ac130-dim); }
```

- [ ] **Step 6: Verify the server starts and /calibrate is reachable**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/calibrate
# Expected: 200
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/
# Expected: 200 (HUD still works)
kill %1
```

- [ ] **Step 7: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js aurore-link/package.json \
        aurore-link/calibrate.html aurore-link/calibrate.css
git commit -m "feat(aurore-link): rename to server.js, add /calibrate route and WS routing"
```

---

## Task 2: MIPI MJPEG stream endpoint

**Files:**
- Modify: `aurore-link/server.js`

- [ ] **Step 1: Add the JPEG boundary scanner helper and MJPEG pipe function**

Add this block near the top of `server.js`, after the `require` statements:

```js
// ---------------------------------------------------------------------------
// MJPEG helpers
// ---------------------------------------------------------------------------

/** Find two-byte sequence (b1, b2) in buf starting at offset. Returns index or -1. */
function findMarker(buf, b1, b2, start = 0) {
  for (let i = start; i < buf.length - 1; i++) {
    if (buf[i] === b1 && buf[i + 1] === b2) return i;
  }
  return -1;
}

/**
 * Pipe a child process stdout (JPEG frames concatenated) to an HTTP response
 * as multipart/x-mixed-replace MJPEG.
 */
function pipeAsMjpeg(res, child) {
  res.writeHead(200, {
    'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
    'Cache-Control': 'no-cache, no-store',
    'Connection': 'keep-alive',
    'Access-Control-Allow-Origin': '*',
  });

  let buf = Buffer.alloc(0);

  child.stdout.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk]);

    let searchFrom = 0;
    while (true) {
      const soiIdx = findMarker(buf, 0xFF, 0xD8, searchFrom);
      if (soiIdx === -1) break;
      const eoiIdx = findMarker(buf, 0xFF, 0xD9, soiIdx + 2);
      if (eoiIdx === -1) break;

      const frame = buf.slice(soiIdx, eoiIdx + 2);
      res.write(
        `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${frame.length}\r\n\r\n`
      );
      res.write(frame);
      res.write('\r\n');
      searchFrom = eoiIdx + 2;
    }

    // Keep only unprocessed tail; cap at 4 MB to avoid unbounded growth
    buf = buf.slice(searchFrom);
    if (buf.length > 4 * 1024 * 1024) buf = buf.slice(-512 * 1024);
  });

  child.on('error', (err) => {
    if (!res.writableEnded) res.end();
    console.error('Camera process error:', err.message);
  });
  child.stdout.on('end', () => { if (!res.writableEnded) res.end(); });
  res.on('close', () => { try { child.kill('SIGTERM'); } catch {} });
}
```

- [ ] **Step 2: Add /stream/mipi route**

Add inside the HTTP request handler, before the final `serveStatic(req, res)` call:

```js
  if (req.url === '/stream/mipi') {
    const { spawn } = require('child_process');
    const child = spawn('libcamera-vid', [
      '--codec', 'mjpeg',
      '--width', '1536',
      '--height', '864',
      '--framerate', '60',
      '--nopreview',
      '--timeout', '0',
      '-o', '-',
    ], { stdio: ['ignore', 'pipe', 'pipe'] });

    child.on('error', () => {
      if (!res.headersSent) {
        res.writeHead(503, { 'Content-Type': 'text/plain' });
        res.end('FAIL: libcamera-vid not available');
      }
    });

    pipeAsMjpeg(res, child);
    return;
  }
```

- [ ] **Step 3: Test the MIPI stream**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
# Receive 3 seconds of MJPEG — expect "Content-Type: multipart/x-mixed-replace" in headers
curl -s --max-time 3 -D - http://localhost:8080/stream/mipi | head -5
# Expected first line: HTTP/1.1 200 OK
# Expected header:     Content-Type: multipart/x-mixed-replace; boundary=frame
kill %1
```

- [ ] **Step 4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js
git commit -m "feat(aurore-link): add /stream/mipi MJPEG endpoint (libcamera-vid 1536x864@60)"
```

---

## Task 3: USB MJPEG stream endpoint

**Files:**
- Modify: `aurore-link/server.js`

- [ ] **Step 1: Add USB camera discovery helper**

Add after the MJPEG helpers block:

```js
// ---------------------------------------------------------------------------
// USB camera discovery (matches usb_camera.cpp: probe /dev/video* for uvcvideo)
// ---------------------------------------------------------------------------
function findUsbCamera() {
  for (let i = 0; i < 10; i++) {
    try {
      const driverPath = `/sys/class/video4linux/video${i}/device/driver`;
      const link = require('fs').readlinkSync(driverPath);
      if (link.endsWith('uvcvideo')) return `/dev/video${i}`;
    } catch {}
  }
  return null;
}
```

- [ ] **Step 2: Add /stream/usb route**

Add in the HTTP request handler alongside the /stream/mipi route:

```js
  if (req.url === '/stream/usb') {
    const usbDev = findUsbCamera();
    if (!usbDev) {
      res.writeHead(503, { 'Content-Type': 'text/plain' });
      res.end('FAIL: USB UVC camera not found on /dev/video0-9');
      return;
    }

    const { spawn } = require('child_process');
    const child = spawn('ffmpeg', [
      '-f', 'v4l2',
      '-input_format', 'mjpeg',
      '-framerate', '60',
      '-video_size', '1280x720',
      '-i', usbDev,
      '-f', 'mjpeg',
      'pipe:1',
    ], { stdio: ['ignore', 'pipe', 'pipe'] });

    child.on('error', () => {
      if (!res.headersSent) {
        res.writeHead(503, { 'Content-Type': 'text/plain' });
        res.end('FAIL: ffmpeg not available');
      }
    });

    pipeAsMjpeg(res, child);
    return;
  }
```

- [ ] **Step 3: Test the USB stream**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
curl -s --max-time 3 -D - http://localhost:8080/stream/usb | head -5
# Expected: HTTP/1.1 200 OK with multipart content-type
# If no USB cam: HTTP/1.1 503 Service Unavailable
kill %1
```

- [ ] **Step 4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js
git commit -m "feat(aurore-link): add /stream/usb MJPEG endpoint (ffmpeg v4l2 1280x720@60)"
```

---

## Task 4: Servo control endpoints

**Files:**
- Modify: `aurore-link/server.js`

The FusionHat sysfs interface (confirmed from source):
- Path: `/sys/class/fusion_hat/fusion_hat/pwm/pwm{N}/`
- Files: `enable` (write `1` to activate), `period` (μs, 20000 for 50Hz), `duty_cycle` (μs, 1000–2000)
- Pan = channel 10, Tilt = channel 11

- [ ] **Step 1: Add servo sysfs helpers**

Add after the USB discovery block:

```js
// ---------------------------------------------------------------------------
// Servo control via FusionHat sysfs
// ---------------------------------------------------------------------------
const SYSFS_PWM_BASE = '/sys/class/fusion_hat/fusion_hat/pwm';
const SERVO_PAN_CH   = 10;
const SERVO_TILT_CH  = 11;
const SERVO_PERIOD_US = 20000;  // 50 Hz

// Server-side state: last commanded angles (degrees, 0–180)
const servoState = { pan: 90, tilt: 90 };

/** Clamp angle to 0–180° and convert to pulse width in microseconds. */
function angleToPulseUs(deg) {
  return Math.round(1000 + (Math.max(0, Math.min(180, deg)) / 180) * 1000);
}

/**
 * Write angle to a single servo channel.
 * Writes period + enable on first use, then only duty_cycle on subsequent calls.
 */
const servoInitialized = new Set();

function writeServoAngle(channel, deg) {
  const pwmDir = `${SYSFS_PWM_BASE}/pwm${channel}`;
  const pulseUs = angleToPulseUs(deg);

  if (!servoInitialized.has(channel)) {
    require('fs').writeFileSync(`${pwmDir}/period`, String(SERVO_PERIOD_US));
    require('fs').writeFileSync(`${pwmDir}/enable`, '1');
    servoInitialized.add(channel);
  }

  require('fs').writeFileSync(`${pwmDir}/duty_cycle`, String(pulseUs));
}
```

- [ ] **Step 2: Add POST /api/servo/center and POST /api/servo/angle**

In the HTTP handler, add before `serveStatic`:

```js
  if (req.method === 'POST' && req.url === '/api/servo/center') {
    try {
      writeServoAngle(SERVO_PAN_CH,  90);
      writeServoAngle(SERVO_TILT_CH, 90);
      servoState.pan  = 90;
      servoState.tilt = 90;
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true, pan_deg: 90, tilt_deg: 90 }));
    } catch (err) {
      res.writeHead(500, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: err.message }));
    }
    return;
  }

  if (req.method === 'POST' && req.url === '/api/servo/angle') {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', () => {
      try {
        const { pan_deg, tilt_deg } = JSON.parse(body);
        if (typeof pan_deg !== 'number' || typeof tilt_deg !== 'number') throw new Error('pan_deg and tilt_deg required');
        const panClamped  = Math.max(0, Math.min(180, pan_deg));
        const tiltClamped = Math.max(0, Math.min(180, tilt_deg));
        writeServoAngle(SERVO_PAN_CH,  panClamped);
        writeServoAngle(SERVO_TILT_CH, tiltClamped);
        servoState.pan  = panClamped;
        servoState.tilt = tiltClamped;
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, pan_deg: panClamped, tilt_deg: tiltClamped }));
      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }
```

- [ ] **Step 3: Test servo centering**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1

# Center both servos
curl -s -X POST http://localhost:8080/api/servo/center
# Expected: {"ok":true,"pan_deg":90,"tilt_deg":90}

# Pan to 45°
curl -s -X POST http://localhost:8080/api/servo/angle \
  -H 'Content-Type: application/json' \
  -d '{"pan_deg":45,"tilt_deg":90}'
# Expected: {"ok":true,"pan_deg":45,"tilt_deg":90}

# Verify sysfs was written (1250 μs = 45°)
cat /sys/class/fusion_hat/fusion_hat/pwm/pwm10/duty_cycle
# Expected: 1250

kill %1
```

- [ ] **Step 4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js
git commit -m "feat(aurore-link): add /api/servo/center and /api/servo/angle sysfs endpoints"
```

---

## Task 5: LRF WebSocket with M01 parser

**Files:**
- Modify: `aurore-link/server.js`

The M01 module outputs 13-byte `0xAA` data frames after each continuous command. Bytes[8:9] are the distance in 4-nibble BCD (mm). Continuous command must be re-sent every ~1.5s of silence.

- [ ] **Step 1: Add M01 frame parser**

Add after the servo helpers:

```js
// ---------------------------------------------------------------------------
// LRF — M01 UART protocol (matches laser_rangefinder.cpp)
// ---------------------------------------------------------------------------
const UART_DEVICE       = '/dev/ttyAMA0';
const CONTINUOUS_CMD    = Buffer.from([0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22]);
const LRF_RESTIM_MS     = 1500;   // re-send continuous command after this many ms idle

/**
 * Parse M01 13-byte data frames from a buffer.
 * Returns { mm, consumed } for the first valid frame found, or null if none.
 *
 * Frame: [0xAA][...][4]=0x00 [5]=0x04 [...][8:9]=BCD mm [10:11]=aux [12]=checksum
 * Checksum = sum(bytes[1..11]) & 0xFF
 */
function parseM01Frame(buf) {
  for (let i = 0; i <= buf.length - 13; i++) {
    if (buf[i] !== 0xAA) continue;
    if (buf[i + 4] !== 0x00 || buf[i + 5] !== 0x04) continue;

    let ck = 0;
    for (let j = 1; j <= 11; j++) ck = (ck + buf[i + j]) & 0xFF;
    if (ck !== buf[i + 12]) continue;

    const dh = buf[i + 8];
    const dl = buf[i + 9];
    const mm = ((dh >> 4) & 0xF) * 1000 +
               (dh & 0xF)         * 100  +
               ((dl >> 4) & 0xF)  * 10   +
               (dl & 0xF);

    return { mm, consumed: i + 13 };
  }
  return null;
}
```

- [ ] **Step 2: Add LRF session state and /ws/calib handler**

Add after the M01 parser:

```js
// LRF session (one per server process — only one calibration client at a time)
const lrfSession = {
  fd: null,
  socket: null,    // net.Socket wrapping the fd
  restimTimer: null,
  lastFrameAt: 0,
};

function lrfSendCmd() {
  if (lrfSession.socket && !lrfSession.socket.destroyed) {
    lrfSession.socket.write(CONTINUOUS_CMD);
    lrfSession.lastFrameAt = Date.now();
  }
}

function lrfOpen() {
  if (lrfSession.fd !== null) return;  // already open
  try {
    const { execSync } = require('child_process');
    execSync(`stty -F ${UART_DEVICE} 9600 cs8 -cstopb -parenb raw -echo -echoe -echok`);
    const fd = require('fs').openSync(UART_DEVICE, require('fs').constants.O_RDWR | require('fs').constants.O_NOCTTY);
    lrfSession.fd = fd;

    const net = require('net');
    const sock = new net.Socket({ fd, readable: true, writable: true, allowHalfOpen: true });
    lrfSession.socket = sock;

    let rxBuf = Buffer.alloc(0);

    sock.on('data', (chunk) => {
      lrfSession.lastFrameAt = Date.now();
      rxBuf = Buffer.concat([rxBuf, chunk]);
      if (rxBuf.length > 256) rxBuf = rxBuf.slice(-128);  // cap

      let result;
      while ((result = parseM01Frame(rxBuf)) !== null) {
        rxBuf = rxBuf.slice(result.consumed);
        // Broadcast to all /ws/calib clients
        calibWss.clients.forEach((ws) => {
          if (ws.readyState === ws.OPEN) {
            ws.send(JSON.stringify({
              lrf_mm:   result.mm,
              pan_deg:  servoState.pan,
              tilt_deg: servoState.tilt,
              ts:       Date.now(),
            }));
          }
        });
      }
    });

    sock.on('error', (err) => {
      console.error('LRF UART error:', err.message);
      lrfClose();
    });

    // Send initial continuous command and start re-stimulation timer
    lrfSendCmd();
    lrfSession.restimTimer = setInterval(() => {
      if (Date.now() - lrfSession.lastFrameAt >= LRF_RESTIM_MS) {
        lrfSendCmd();
      }
    }, 500);

  } catch (err) {
    console.error('LRF open failed:', err.message);
    lrfSession.fd = null;
    // Notify clients
    calibWss.clients.forEach((ws) => {
      if (ws.readyState === ws.OPEN) {
        ws.send(JSON.stringify({ error: 'lrf_unavailable', detail: err.message }));
      }
    });
  }
}

function lrfClose() {
  clearInterval(lrfSession.restimTimer);
  lrfSession.restimTimer = null;
  if (lrfSession.socket) {
    try { lrfSession.socket.destroy(); } catch {}
    lrfSession.socket = null;
  }
  if (lrfSession.fd !== null) {
    try { require('fs').closeSync(lrfSession.fd); } catch {}
    lrfSession.fd = null;
  }
}
```

- [ ] **Step 3: Wire up calibWss connection handler**

Add after the `server.on('upgrade', ...)` block:

```js
calibWss.on('connection', (ws) => {
  // Send current servo state immediately on connect
  ws.send(JSON.stringify({
    lrf_mm:   null,
    pan_deg:  servoState.pan,
    tilt_deg: servoState.tilt,
    ts:       Date.now(),
  }));

  ws.on('message', (raw) => {
    try {
      const msg = JSON.parse(raw.toString());
      if (msg.type === 'lrf_start') lrfOpen();
      if (msg.type === 'lrf_stop')  lrfClose();
    } catch {}
  });

  ws.on('close', () => {
    // Close LRF if no more calib clients
    if (calibWss.clients.size === 0) lrfClose();
  });
});
```

- [ ] **Step 4: Servo state push — also broadcast servo angles on every /api/servo/* call**

After each successful `writeServoAngle` call in both servo endpoints (both `center` and `angle`), add a broadcast:

```js
// After updating servoState in both servo endpoints:
calibWss.clients.forEach((ws) => {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify({
      lrf_mm:   null,
      pan_deg:  servoState.pan,
      tilt_deg: servoState.tilt,
      ts:       Date.now(),
    }));
  }
});
```

- [ ] **Step 5: Test the calibration WebSocket**

Install `websocat` if not present: `sudo apt-get install -y websocat` (or use `wscat`: `sudo npm install -g wscat`).

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1

# In a second terminal:
wscat -c ws://localhost:8080/ws/calib
# Expected on connect: {"lrf_mm":null,"pan_deg":90,"tilt_deg":90,"ts":...}
# Type: {"type":"lrf_start"}
# Expected stream: {"lrf_mm":985,"pan_deg":90,"tilt_deg":90,"ts":...}

kill %1
```

- [ ] **Step 6: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js
git commit -m "feat(aurore-link): add /ws/calib WebSocket with M01 LRF parser and servo state push"
```

---

## Task 6: calibrate.html full layout + calibrate.css

**Files:**
- Modify: `aurore-link/calibrate.html` (replace skeleton with full layout)
- Modify: `aurore-link/calibrate.css` (replace skeleton with full styles)

- [ ] **Step 1: Write full calibrate.html**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Aurore MkVII — Calibration</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="style.css">
  <link rel="stylesheet" href="calibrate.css">
</head>
<body>
  <div id="cal-app">

    <!-- Header -->
    <header id="cal-header">
      <a href="/" id="back-link">← HUD</a>
      <span id="cal-title">CALIBRATION MODE</span>
      <span id="cal-status"></span>
    </header>

    <!-- Dual camera feeds -->
    <section id="feed-section">
      <div class="feed-wrap" id="feed-mipi-wrap">
        <div class="feed-label">MIPI CAM</div>
        <div class="feed-container">
          <img id="feed-mipi" src="/stream/mipi" alt="MIPI feed">
          <svg class="crosshair" id="xhair-mipi" viewBox="0 0 100 100" preserveAspectRatio="none">
            <line x1="50" y1="0"  x2="50" y2="42"  class="xh-line"/>
            <line x1="50" y1="58" x2="50" y2="100" class="xh-line"/>
            <line x1="0"  y1="50" x2="42"  y2="50" class="xh-line"/>
            <line x1="58" y1="50" x2="100" y2="50" class="xh-line"/>
            <circle cx="50" cy="50" r="8" class="xh-circle"/>
          </svg>
        </div>
        <div class="lrf-badge" id="lrf-badge-mipi">LRF: --</div>
      </div>

      <div class="feed-wrap" id="feed-usb-wrap">
        <div class="feed-label">USB CAM</div>
        <div class="feed-container">
          <img id="feed-usb" src="/stream/usb" alt="USB feed">
          <svg class="crosshair" id="xhair-usb" viewBox="0 0 100 100" preserveAspectRatio="none">
            <line x1="50" y1="0"  x2="50" y2="42"  class="xh-line"/>
            <line x1="50" y1="58" x2="50" y2="100" class="xh-line"/>
            <line x1="0"  y1="50" x2="42"  y2="50" class="xh-line"/>
            <line x1="58" y1="50" x2="100" y2="50" class="xh-line"/>
            <circle cx="50" cy="50" r="8" class="xh-circle"/>
          </svg>
        </div>
        <div class="lrf-badge" id="lrf-badge-usb">LRF: --</div>
      </div>
    </section>

    <!-- Step tabs -->
    <nav id="step-tabs">
      <button class="step-tab active" id="tab-1" data-step="1">1 — CENTER SERVOS</button>
      <button class="step-tab" id="tab-2" data-step="2" disabled>2 — LRF ALIGN</button>
      <button class="step-tab" id="tab-3" data-step="3" disabled>3 — CAM OFFSET</button>
    </nav>

    <!-- Step panels -->
    <section id="step-panels">

      <!-- Step 1: Servo centering -->
      <div class="step-panel active" id="panel-1">
        <div id="angle-display">
          PAN <span id="pan-val">---</span>° &nbsp; TILT <span id="tilt-val">---</span>°
        </div>
        <div class="jog-section">
          <div class="jog-row">
            <span class="jog-label">PAN</span>
            <button class="jog-btn" data-axis="pan" data-delta="-10">-10°</button>
            <button class="jog-btn" data-axis="pan" data-delta="-5">-5°</button>
            <button class="jog-btn" data-axis="pan" data-delta="-1">-1°</button>
            <button class="jog-btn center-btn" id="btn-center">CENTER</button>
            <button class="jog-btn" data-axis="pan" data-delta="1">+1°</button>
            <button class="jog-btn" data-axis="pan" data-delta="5">+5°</button>
            <button class="jog-btn" data-axis="pan" data-delta="10">+10°</button>
          </div>
          <div class="jog-row">
            <span class="jog-label">TILT</span>
            <button class="jog-btn" data-axis="tilt" data-delta="-10">-10°</button>
            <button class="jog-btn" data-axis="tilt" data-delta="-5">-5°</button>
            <button class="jog-btn" data-axis="tilt" data-delta="-1">-1°</button>
            <span class="jog-spacer"></span>
            <button class="jog-btn" data-axis="tilt" data-delta="1">+1°</button>
            <button class="jog-btn" data-axis="tilt" data-delta="5">+5°</button>
            <button class="jog-btn" data-axis="tilt" data-delta="10">+10°</button>
          </div>
        </div>
        <button class="action-btn" id="btn-step1-done">DONE — SERVOS CENTRED ▶</button>
      </div>

      <!-- Step 2: LRF alignment -->
      <div class="step-panel" id="panel-2">
        <div id="lrf-display">LRF: <span id="lrf-val">--</span> mm</div>
        <div class="step2-controls">
          <button class="action-btn" id="btn-lrf-toggle">ACTIVATE LRF</button>
          <div class="jog-section">
            <div class="jog-row">
              <span class="jog-label">PAN</span>
              <button class="jog-btn" data-axis="pan" data-delta="-10">-10°</button>
              <button class="jog-btn" data-axis="pan" data-delta="-5">-5°</button>
              <button class="jog-btn" data-axis="pan" data-delta="-1">-1°</button>
              <button class="jog-btn" data-axis="pan" data-delta="1">+1°</button>
              <button class="jog-btn" data-axis="pan" data-delta="5">+5°</button>
              <button class="jog-btn" data-axis="pan" data-delta="10">+10°</button>
            </div>
            <div class="jog-row">
              <span class="jog-label">TILT</span>
              <button class="jog-btn" data-axis="tilt" data-delta="-10">-10°</button>
              <button class="jog-btn" data-axis="tilt" data-delta="-5">-5°</button>
              <button class="jog-btn" data-axis="tilt" data-delta="-1">-1°</button>
              <button class="jog-btn" data-axis="tilt" data-delta="1">+1°</button>
              <button class="jog-btn" data-axis="tilt" data-delta="5">+5°</button>
              <button class="jog-btn" data-axis="tilt" data-delta="10">+10°</button>
            </div>
          </div>
          <button class="action-btn" id="btn-mark-aligned" disabled>MARK ALIGNED</button>
          <button class="action-btn" id="btn-save-centre" disabled>SAVE CENTRE OFFSET ▶</button>
        </div>
      </div>

      <!-- Step 3: Camera offset -->
      <div class="step-panel" id="panel-3">
        <p class="step-instruction">Click a point on the MIPI feed, then the same point on the USB feed.</p>
        <div id="click-status">
          MIPI click: <span id="mipi-click">--</span> &nbsp;
          USB click: <span id="usb-click">--</span>
        </div>
        <div id="offset-pairs"></div>
        <div id="offset-result">Offset: <span id="offset-val">--</span></div>
        <button class="action-btn" id="btn-save-offset" disabled>SAVE CAMERA OFFSET</button>
      </div>

    </section>

    <!-- Save bar -->
    <footer id="save-bar">
      <button class="action-btn save-all-btn" id="btn-save-all" disabled>SAVE ALL CALIBRATION DATA</button>
      <span id="save-status"></span>
    </footer>

  </div>
  <script src="calibrate.js"></script>
</body>
</html>
```

- [ ] **Step 2: Write full calibrate.css**

```css
/* Calibration page styles — reuses :root variables from style.css */

body { overflow-y: auto; }

#cal-app {
  min-height: 100vh;
  background: var(--bg);
  color: var(--ac130-white);
  font-family: 'Share Tech Mono', monospace;
  display: flex;
  flex-direction: column;
}

/* Header */
#cal-header {
  display: flex;
  align-items: center;
  gap: 1.5rem;
  padding: 0.6rem 1.5rem;
  border-bottom: 1px solid rgba(255,255,255,0.15);
  background: #111;
  flex-shrink: 0;
}
#back-link { color: var(--ac130-dim); text-decoration: none; font-size: 0.85rem; }
#back-link:hover { color: var(--ac130-white); }
#cal-title { flex: 1; text-align: center; letter-spacing: 0.2em; font-size: 0.9rem; }
#cal-status { font-size: 0.7rem; color: var(--ac130-dim); min-width: 8ch; text-align: right; }

/* Camera feeds */
#feed-section {
  display: flex;
  gap: 0.5rem;
  padding: 0.5rem;
  background: #050505;
  flex-shrink: 0;
}

.feed-wrap {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.feed-label {
  font-size: 0.7rem;
  color: var(--ac130-dim);
  letter-spacing: 0.1em;
  padding-left: 0.25rem;
}

.feed-container {
  position: relative;
  width: 100%;
  aspect-ratio: 16/9;
  background: #1a1a1a;
  overflow: hidden;
}

.feed-container img {
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

/* Crosshair SVG overlay */
.crosshair {
  position: absolute;
  top: 0; left: 0;
  width: 100%; height: 100%;
  pointer-events: none;
}

.xh-line   { stroke: rgba(255,255,255,0.6); stroke-width: 0.5; }
.xh-circle { fill: none; stroke: rgba(255,255,255,0.6); stroke-width: 0.5; }

.crosshair.aligned .xh-line,
.crosshair.aligned .xh-circle { stroke: rgba(0,255,120,0.85); }

/* Click point markers for Step 3 */
.click-dot {
  position: absolute;
  width: 10px; height: 10px;
  border-radius: 50%;
  background: #ff4400;
  transform: translate(-50%, -50%);
  pointer-events: none;
}

.lrf-badge {
  font-size: 0.75rem;
  color: var(--ac130-dim);
  text-align: center;
  padding: 0.1rem;
}
.lrf-badge.active { color: #00ff80; }

/* Step tabs */
#step-tabs {
  display: flex;
  border-bottom: 1px solid rgba(255,255,255,0.15);
  background: #111;
  flex-shrink: 0;
}

.step-tab {
  flex: 1;
  background: transparent;
  border: none;
  border-right: 1px solid rgba(255,255,255,0.1);
  color: var(--ac130-dim);
  font-family: inherit;
  font-size: 0.75rem;
  letter-spacing: 0.1em;
  padding: 0.6rem;
  cursor: pointer;
  transition: background 0.15s;
}
.step-tab:last-child { border-right: none; }
.step-tab.active { color: var(--ac130-white); background: #1e1e1e; }
.step-tab:disabled { opacity: 0.35; cursor: not-allowed; }
.step-tab:not(:disabled):hover { background: #1a1a1a; }

/* Step panels */
#step-panels { flex: 1; padding: 1rem 1.5rem; }

.step-panel { display: none; flex-direction: column; gap: 1rem; }
.step-panel.active { display: flex; }

/* Angle display */
#angle-display, #lrf-display {
  font-size: 1rem;
  letter-spacing: 0.1em;
  color: #00ff80;
  text-align: center;
  padding: 0.25rem;
}

/* Jog controls */
.jog-section { display: flex; flex-direction: column; gap: 0.5rem; }
.jog-row {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  flex-wrap: wrap;
}
.jog-label { min-width: 3.5ch; font-size: 0.8rem; color: var(--ac130-dim); }
.jog-spacer { flex: 1; min-width: 4rem; }

.jog-btn {
  background: #1e1e1e;
  border: 1px solid rgba(255,255,255,0.25);
  color: var(--ac130-white);
  font-family: inherit;
  font-size: 0.8rem;
  padding: 0.35rem 0.6rem;
  cursor: pointer;
  min-width: 4rem;
}
.jog-btn:hover { background: #2e2e2e; border-color: rgba(255,255,255,0.5); }
.jog-btn:active { background: #3e3e3e; }

.center-btn {
  background: #1e3a1e;
  border-color: rgba(0,255,120,0.4);
  min-width: 6rem;
}
.center-btn:hover { background: #2a4a2a; }

/* Action buttons */
.action-btn {
  background: #1a1a1a;
  border: 1px solid rgba(255,255,255,0.35);
  color: var(--ac130-white);
  font-family: inherit;
  font-size: 0.85rem;
  letter-spacing: 0.1em;
  padding: 0.6rem 1.2rem;
  cursor: pointer;
  align-self: flex-start;
}
.action-btn:hover:not(:disabled) { background: #252525; border-color: rgba(255,255,255,0.6); }
.action-btn:disabled { opacity: 0.3; cursor: not-allowed; }
.action-btn.active { border-color: #00ff80; color: #00ff80; }

/* Step instruction */
.step-instruction { font-size: 0.8rem; color: var(--ac130-dim); }

/* Click status */
#click-status { font-size: 0.8rem; color: var(--ac130-dim); }
#offset-pairs { font-size: 0.75rem; color: var(--ac130-dim); display: flex; flex-direction: column; gap: 0.2rem; }
#offset-result { font-size: 0.9rem; color: #00ff80; }

/* Save bar */
#save-bar {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem 1.5rem;
  border-top: 1px solid rgba(255,255,255,0.15);
  background: #111;
  flex-shrink: 0;
}
.save-all-btn { align-self: auto; }
#save-status { font-size: 0.8rem; color: var(--ac130-dim); }

/* Step 2 */
.step2-controls { display: flex; flex-direction: column; gap: 0.75rem; }

/* Responsive: stack feeds on narrow screens */
@media (max-width: 768px) {
  #feed-section { flex-direction: column; }
  .jog-row { gap: 0.3rem; }
  .jog-btn { min-width: 3rem; font-size: 0.75rem; padding: 0.3rem 0.4rem; }
}
```

- [ ] **Step 3: Verify page renders in browser**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
# Open http://localhost:8080/calibrate in a browser
# Verify: header, two feed containers, three step tabs, Step 1 controls visible
# Verify: Step 2 and Step 3 tabs are greyed out (disabled)
kill %1
```

- [ ] **Step 4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/calibrate.html aurore-link/calibrate.css
git commit -m "feat(aurore-link): add calibrate.html layout and calibrate.css"
```

---

## Task 7: calibrate.js — WebSocket + Step 1 (servo centering)

**Files:**
- Create: `aurore-link/calibrate.js`

- [ ] **Step 1: Write calibrate.js with WebSocket, servo state, and Step 1 logic**

```js
'use strict';

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const state = {
  pan:  90,
  tilt: 90,
  lrfMm: null,
  lrfActive: false,
  step: 1,                    // current active step (1, 2, 3)
  step1Done: false,
  step2Aligned: null,         // { pan_deg, tilt_deg, lrf_mm } or null
  step2Done: false,
  cameraOffsetPairs: [],      // [{mipi: {x,y}, usb: {x,y}}]
  pendingMipiClick: null,     // {x, y} waiting for matching USB click
  cameraOffset: null,         // averaged {dx, dy}
};

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
const proto = location.protocol === 'https:' ? 'wss' : 'ws';
const ws = new WebSocket(`${proto}://${location.host}/ws/calib`);

ws.addEventListener('message', (ev) => {
  try {
    const msg = JSON.parse(ev.data);
    if (msg.error) {
      setStatus(`ERROR: ${msg.error}`);
      return;
    }
    if (typeof msg.pan_deg  === 'number') { state.pan  = msg.pan_deg; }
    if (typeof msg.tilt_deg === 'number') { state.tilt = msg.tilt_deg; }
    if (typeof msg.lrf_mm   === 'number' && msg.lrf_mm > 0) {
      state.lrfMm = msg.lrf_mm;
    }
    renderAngles();
    renderLrf();
  } catch {}
});

ws.addEventListener('open',  () => setStatus('WS OK'));
ws.addEventListener('close', () => setStatus('WS DISCONNECTED'));
ws.addEventListener('error', () => setStatus('WS ERROR'));

// ---------------------------------------------------------------------------
// DOM helpers
// ---------------------------------------------------------------------------
const $ = (id) => document.getElementById(id);

function setStatus(text) {
  $('cal-status').textContent = text;
}

function renderAngles() {
  $('pan-val').textContent  = state.pan.toFixed(1);
  $('tilt-val').textContent = state.tilt.toFixed(1);
}

function renderLrf() {
  const mmText = state.lrfMm !== null ? `${state.lrfMm} mm` : '--';
  $('lrf-val').textContent       = state.lrfMm !== null ? state.lrfMm : '--';
  $('lrf-badge-mipi').textContent = `LRF: ${mmText}`;
  $('lrf-badge-usb').textContent  = `LRF: ${mmText}`;

  if (state.lrfActive) {
    $('lrf-badge-mipi').classList.add('active');
    $('lrf-badge-usb').classList.add('active');
  } else {
    $('lrf-badge-mipi').classList.remove('active');
    $('lrf-badge-usb').classList.remove('active');
  }
}

// ---------------------------------------------------------------------------
// Servo commands
// ---------------------------------------------------------------------------
async function postJson(url, body) {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return res.json();
}

async function centerServos() {
  const data = await postJson('/api/servo/center', {});
  if (data.ok) {
    state.pan  = data.pan_deg;
    state.tilt = data.tilt_deg;
    renderAngles();
  } else {
    setStatus(`SERVO ERROR: ${data.error}`);
  }
}

async function jogServo(axis, deltaDeg) {
  const newPan  = axis === 'pan'  ? state.pan  + deltaDeg : state.pan;
  const newTilt = axis === 'tilt' ? state.tilt + deltaDeg : state.tilt;
  const data = await postJson('/api/servo/angle', { pan_deg: newPan, tilt_deg: newTilt });
  if (data.ok) {
    state.pan  = data.pan_deg;
    state.tilt = data.tilt_deg;
    renderAngles();
  } else {
    setStatus(`SERVO ERROR: ${data.error}`);
  }
}

// ---------------------------------------------------------------------------
// Step management
// ---------------------------------------------------------------------------
function activateStep(n) {
  state.step = n;
  document.querySelectorAll('.step-panel').forEach((p, i) => {
    p.classList.toggle('active', i + 1 === n);
  });
  document.querySelectorAll('.step-tab').forEach((t) => {
    t.classList.toggle('active', Number(t.dataset.step) === n);
  });
}

// ---------------------------------------------------------------------------
// Step 1: Servo centering
// ---------------------------------------------------------------------------
$('btn-center').addEventListener('click', centerServos);

document.querySelectorAll('.jog-btn[data-axis]').forEach((btn) => {
  btn.addEventListener('click', () => {
    jogServo(btn.dataset.axis, Number(btn.dataset.delta));
  });
});

$('btn-step1-done').addEventListener('click', () => {
  state.step1Done = true;
  $('tab-2').disabled = false;
  activateStep(2);
});

// ---------------------------------------------------------------------------
// Step tab clicks
// ---------------------------------------------------------------------------
document.querySelectorAll('.step-tab').forEach((tab) => {
  tab.addEventListener('click', () => {
    const n = Number(tab.dataset.step);
    if (!tab.disabled) activateStep(n);
  });
});
```

- [ ] **Step 2: Verify Step 1 in browser**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
# Open http://localhost:8080/calibrate
# - Pan/Tilt angles update after WebSocket connects
# - CENTER button moves servos to 90°/90°
# - Jog buttons adjust angle by stated delta
# - "DONE — SERVOS CENTRED" enables and activates Step 2 tab
kill %1
```

- [ ] **Step 3: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/calibrate.js
git commit -m "feat(aurore-link): calibrate.js WebSocket + Step 1 servo centering"
```

---

## Task 8: calibrate.js — Step 2 (LRF alignment)

**Files:**
- Modify: `aurore-link/calibrate.js`

- [ ] **Step 1: Add LRF toggle and Step 2 logic**

Append to `calibrate.js`:

```js
// ---------------------------------------------------------------------------
// Step 2: LRF alignment
// ---------------------------------------------------------------------------
$('btn-lrf-toggle').addEventListener('click', () => {
  state.lrfActive = !state.lrfActive;
  ws.send(JSON.stringify({ type: state.lrfActive ? 'lrf_start' : 'lrf_stop' }));
  $('btn-lrf-toggle').textContent = state.lrfActive ? 'DEACTIVATE LRF' : 'ACTIVATE LRF';
  $('btn-lrf-toggle').classList.toggle('active', state.lrfActive);
  $('btn-mark-aligned').disabled = !state.lrfActive;
  renderLrf();
});

$('btn-mark-aligned').addEventListener('click', () => {
  if (state.lrfMm === null) {
    setStatus('No LRF reading yet');
    return;
  }
  state.step2Aligned = { pan_deg: state.pan, tilt_deg: state.tilt, lrf_mm: state.lrfMm };

  // Turn crosshairs green
  document.querySelectorAll('.crosshair').forEach((el) => el.classList.add('aligned'));

  $('btn-save-centre').disabled = false;
  setStatus(`Aligned: ${state.lrfMm} mm @ pan=${state.pan.toFixed(1)}° tilt=${state.tilt.toFixed(1)}°`);
});

$('btn-save-centre').addEventListener('click', () => {
  if (!state.step2Aligned) return;
  state.step2Done = true;
  $('tab-3').disabled = false;
  activateStep(3);
  updateSaveAllButton();
});
```

- [ ] **Step 2: Verify Step 2 in browser**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1
# Open http://localhost:8080/calibrate, complete Step 1
# - Activate LRF: distance badge appears on both feeds, "Mark Aligned" enables
# - Jog servos: angles update live
# - Mark Aligned: status shows reading + angles, crosshairs turn green
# - Save Centre Offset: Step 3 tab enables and activates
kill %1
```

- [ ] **Step 3: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/calibrate.js
git commit -m "feat(aurore-link): calibrate.js Step 2 LRF alignment with crosshair feedback"
```

---

## Task 9: calibrate.js — Step 3 (camera-to-camera pixel offset)

**Files:**
- Modify: `aurore-link/calibrate.js`

- [ ] **Step 1: Add click handlers and offset computation**

Append to `calibrate.js`:

```js
// ---------------------------------------------------------------------------
// Step 3: Camera-to-camera pixel offset
// ---------------------------------------------------------------------------

/**
 * Translate a click event on a feed-container img to relative [0,1] coordinates,
 * then to pixel coordinates in the camera's native resolution.
 * MIPI native: 1536×864. USB native: 1280×720.
 */
function clickToPixel(ev, nativeW, nativeH) {
  const rect = ev.target.getBoundingClientRect();
  const relX = (ev.clientX - rect.left)  / rect.width;
  const relY = (ev.clientY - rect.top)   / rect.height;
  return { x: Math.round(relX * nativeW), y: Math.round(relY * nativeH) };
}

function renderOffsetPairs() {
  const el = $('offset-pairs');
  el.innerHTML = '';
  state.cameraOffsetPairs.forEach((p, i) => {
    const div = document.createElement('div');
    div.textContent = `[${i + 1}] MIPI(${p.mipi.x},${p.mipi.y}) USB(${p.usb.x},${p.usb.y})`
                    + ` → Δ(${p.usb.x - p.mipi.x}, ${p.usb.y - p.mipi.y})`;
    el.appendChild(div);
  });
}

function computeAverageOffset() {
  if (state.cameraOffsetPairs.length === 0) return null;
  let sumDx = 0, sumDy = 0;
  state.cameraOffsetPairs.forEach((p) => {
    sumDx += p.usb.x - p.mipi.x;
    sumDy += p.usb.y - p.mipi.y;
  });
  const n = state.cameraOffsetPairs.length;
  return { dx: Math.round(sumDx / n), dy: Math.round(sumDy / n) };
}

function updateOffsetDisplay() {
  state.cameraOffset = computeAverageOffset();
  if (state.cameraOffset) {
    $('offset-val').textContent = `dx=${state.cameraOffset.dx}px dy=${state.cameraOffset.dy}px (n=${state.cameraOffsetPairs.length})`;
    $('btn-save-offset').disabled = false;
  }
}

// Clicking MIPI feed captures first point
$('feed-mipi').addEventListener('click', (ev) => {
  if (state.step !== 3) return;
  state.pendingMipiClick = clickToPixel(ev, 1536, 864);
  $('mipi-click').textContent = `(${state.pendingMipiClick.x}, ${state.pendingMipiClick.y})`;
  $('usb-click').textContent  = 'waiting...';

  // Show red dot on MIPI feed
  document.querySelectorAll('#feed-mipi-wrap .click-dot').forEach((d) => d.remove());
  const dot = document.createElement('div');
  dot.className = 'click-dot';
  dot.style.left = `${(state.pendingMipiClick.x / 1536) * 100}%`;
  dot.style.top  = `${(state.pendingMipiClick.y / 864)  * 100}%`;
  $('feed-mipi-wrap').querySelector('.feed-container').appendChild(dot);
});

// Clicking USB feed completes the pair
$('feed-usb').addEventListener('click', (ev) => {
  if (state.step !== 3 || !state.pendingMipiClick) return;
  const usbPx = clickToPixel(ev, 1280, 720);
  $('usb-click').textContent = `(${usbPx.x}, ${usbPx.y})`;

  // Show red dot on USB feed
  document.querySelectorAll('#feed-usb-wrap .click-dot').forEach((d) => d.remove());
  const dot = document.createElement('div');
  dot.className = 'click-dot';
  dot.style.left = `${(usbPx.x / 1280) * 100}%`;
  dot.style.top  = `${(usbPx.y / 720)  * 100}%`;
  $('feed-usb-wrap').querySelector('.feed-container').appendChild(dot);

  state.cameraOffsetPairs.push({ mipi: state.pendingMipiClick, usb: usbPx });
  state.pendingMipiClick = null;

  renderOffsetPairs();
  updateOffsetDisplay();
});

$('btn-save-offset').addEventListener('click', () => {
  if (!state.cameraOffset) return;
  updateSaveAllButton();
  setStatus(`Camera offset saved: dx=${state.cameraOffset.dx} dy=${state.cameraOffset.dy}`);
});

// Cursor hint: crosshair on feeds during Step 3
document.querySelectorAll('#feed-mipi, #feed-usb').forEach((img) => {
  img.style.cursor = 'crosshair';
});
```

- [ ] **Step 2: Add `updateSaveAllButton` helper (called from multiple steps)**

Add this near the top of `calibrate.js`, after the state declaration:

```js
function updateSaveAllButton() {
  $('btn-save-all').disabled = !(state.step1Done && state.step2Done);
}
```

- [ ] **Step 3: Verify Step 3 in browser**

```bash
# After completing Steps 1 and 2:
# - Step 3 panel shows
# - Clicking MIPI feed shows red dot and "(x, y)" coordinates
# - Clicking USB feed shows red dot and completes pair
# - Offset pair listed below: "MIPI(768,432) USB(782,430) → Δ(14, -2)"
# - Average offset updates after each pair
# - "Save Camera Offset" enables and marks offset in state
```

- [ ] **Step 4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/calibrate.js
git commit -m "feat(aurore-link): calibrate.js Step 3 camera pixel offset with click pairs"
```

---

## Task 10: Save calibration data endpoint + Save button

**Files:**
- Modify: `aurore-link/server.js`
- Modify: `aurore-link/calibrate.js`

- [ ] **Step 1: Add POST /api/calibration/save endpoint to server.js**

Add to the HTTP request handler, before `serveStatic`:

```js
  if (req.method === 'POST' && req.url === '/api/calibration/save') {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', () => {
      try {
        const payload = JSON.parse(body);

        // Validate required fields
        if (!payload.servo || !payload.lrf) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'servo and lrf blocks required' }));
          return;
        }

        const outDir  = require('path').join(__dirname, '..', 'config');
        const outPath = require('path').join(outDir, 'calibration.json');

        require('fs').mkdirSync(outDir, { recursive: true });
        require('fs').writeFileSync(outPath, JSON.stringify(payload, null, 2));

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, path: outPath }));
      } catch (err) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }
```

- [ ] **Step 2: Add Save All button handler to calibrate.js**

Append to `calibrate.js`:

```js
// ---------------------------------------------------------------------------
// Save all calibration data
// ---------------------------------------------------------------------------
$('btn-save-all').addEventListener('click', async () => {
  const payload = {
    saved_at: new Date().toISOString(),
    servo: {
      pan_channel:    10,
      tilt_channel:   11,
      pan_center_deg:  state.pan,
      tilt_center_deg: state.tilt,
    },
    lrf: state.step2Aligned ? {
      pan_at_center_deg:    state.step2Aligned.pan_deg,
      tilt_at_center_deg:   state.step2Aligned.tilt_deg,
      distance_at_center_mm: state.step2Aligned.lrf_mm,
    } : null,
    cameras: state.cameraOffset ? {
      pixel_offset_x: state.cameraOffset.dx,
      pixel_offset_y: state.cameraOffset.dy,
      sample_count:   state.cameraOffsetPairs.length,
    } : null,
  };

  try {
    const res = await fetch('/api/calibration/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const data = await res.json();
    if (data.ok) {
      $('save-status').textContent = `Saved to ${data.path} at ${new Date().toLocaleTimeString()}`;
      $('save-status').style.color = '#00ff80';
    } else {
      $('save-status').textContent = `SAVE FAILED: ${data.error}`;
      $('save-status').style.color = '#ff4444';
    }
  } catch (err) {
    $('save-status').textContent = `SAVE FAILED: ${err.message}`;
    $('save-status').style.color = '#ff4444';
  }
});
```

- [ ] **Step 3: End-to-end save test**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js &
sleep 1

# Test save endpoint directly
curl -s -X POST http://localhost:8080/api/calibration/save \
  -H 'Content-Type: application/json' \
  -d '{
    "saved_at":"2026-03-31T12:00:00.000Z",
    "servo":{"pan_channel":10,"tilt_channel":11,"pan_center_deg":90,"tilt_center_deg":90},
    "lrf":{"pan_at_center_deg":90,"tilt_at_center_deg":90,"distance_at_center_mm":985},
    "cameras":null
  }'
# Expected: {"ok":true,"path":"/home/pi/AuroreMkVII/config/calibration.json"}

cat /home/pi/AuroreMkVII/config/calibration.json
# Expected: pretty-printed JSON matching input

kill %1
```

- [ ] **Step 4: Full end-to-end browser test**

```bash
cd /home/pi/AuroreMkVII/aurore-link
node server.js
# Open http://localhost:8080/calibrate in browser
# 1. Step 1: click CENTER, then DONE — SERVOS CENTRED
# 2. Step 2: ACTIVATE LRF, wait for distance reading, MARK ALIGNED, SAVE CENTRE OFFSET
# 3. Step 3: click MIPI, click USB (repeat once), SAVE CAMERA OFFSET
# 4. Save bar: SAVE ALL CALIBRATION DATA
# Verify: status bar shows saved path, config/calibration.json exists with all blocks filled
```

- [ ] **Step 5: Update CLAUDE.md for aurore-link**

In `aurore-link/CLAUDE.md`, update the File Map table:

```markdown
| File | Purpose |
|------|---------|
| `server.js` | Node.js HTTP static server + WebSocket on port 8080 (renamed from mock-server.js) |
| `index.html` | HUD SPA shell: canvas, SVG HUD, sidebar/strip |
| `style.css` | Responsive layout, CSS variables, HUD styles |
| `main.js` | WS client, canvas animation, SVG HUD renderer, controls |
| `calibrate.html` | Calibration page shell: dual feeds, step tabs, save bar |
| `calibrate.css` | Calibration page styles (reuses :root vars from style.css) |
| `calibrate.js` | Calibration workflow: servo jog, LRF toggle, camera offset, save |
```

Also update the Running section:
```markdown
## Running
```bash
cd /home/pi/AuroreMkVII/aurore-link
npm install
node server.js
# HUD:         http://localhost:8080/
# Calibration: http://localhost:8080/calibrate
```
```

- [ ] **Step 6: Commit**

```bash
cd /home/pi/AuroreMkVII
git add aurore-link/server.js aurore-link/calibrate.js aurore-link/CLAUDE.md
git commit -m "feat(aurore-link): add calibration save endpoint and complete end-to-end workflow"
```

---

## Self-Review Checklist

| Spec requirement | Task covering it |
|-----------------|-----------------|
| Rename mock-server.js → server.js | Task 1 |
| /calibrate page route | Task 1 |
| MIPI MJPEG 1536×864@60fps | Task 2 |
| USB MJPEG 1280×720@60fps | Task 3 |
| USB device discovery (uvcvideo) | Task 3 |
| POST /api/servo/center | Task 4 |
| POST /api/servo/angle (clamped) | Task 4 |
| Sysfs servo write (μs, correct path) | Task 4 |
| /ws/calib WebSocket | Task 5 |
| M01 13-byte frame parser in JS | Task 5 |
| LRF re-stimulation every 1.5s | Task 5 |
| lrf_start / lrf_stop toggle | Task 5, 8 |
| Crosshair SVG overlay (both feeds) | Task 6 |
| Step tabs (sequential unlock) | Task 6, 7 |
| Step 1: Center + jog controls | Task 7 |
| Step 2: LRF distance display | Task 8 |
| Step 2: Mark Aligned + crosshairs green | Task 8 |
| Step 3: Click pairs + pixel offset | Task 9 |
| Step 3: Average multiple pairs | Task 9 |
| POST /api/calibration/save | Task 10 |
| config/calibration.json format | Task 10 |
| Save enabled after Steps 1+2 | Task 10 |
| No mocks anywhere | All tasks — all endpoints return errors if hardware absent |
| No new npm dependencies | Confirmed — only `ws` (already installed) used |
| Vanilla JS/CSS | Confirmed |
| Responsive ≥768px | Task 6 (media query) |
