/**
 * Aurore MkVII — Remote Control & Calibration Server
 * Serves HUD interface, calibration page, MJPEG camera streams, and WebSocket telemetry.
 *
 * Usage: node server.js
 */

'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const net = require('net');
const { spawn, execSync } = require('child_process');
const { WebSocketServer } = require('ws');

const PORT = 8080;
const TELEMETRY_INTERVAL_MS = 150;
const STATIC_ROOT = __dirname;
const HUD_SOCKET_PATH = '/run/aurore/hud_telemetry.sock';
const HUD_RECONNECT_MS = 2000;
const CMD_SOCKET_PATH = '/tmp/aurore_cmd.sock';
const CMD_RECONNECT_MS = 2000;

// FCS state enum (must match C++ FcsState: BOOT=0, IDLE_SAFE=1, FREECAM=2, SEARCH=3, TRACKING=4, ARMED=5, FAULT=6)
const FCS_STATES = ['BOOT', 'IDLE_SAFE', 'FREECAM', 'SEARCH', 'TRACKING', 'ARMED', 'FAULT'];
const CANVAS_W = 1536;
const CANVAS_H = 864;

// ===========================================================================
// MJPEG Helpers
// ===========================================================================

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
 *
 * Latency design: when multiple complete frames arrive in a burst (e.g. after
 * a pipeline stall), only the LAST complete frame is sent and the rest are
 * discarded. This keeps the stream at the live edge rather than draining a
 * stale backlog.
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

    // Find ALL complete frames, keep only the last one to avoid sending stale backlog
    let lastFrameStart = -1;
    let lastFrameEnd = -1;
    let searchFrom = 0;
    while (true) {
      const soiIdx = findMarker(buf, 0xFF, 0xD8, searchFrom);
      if (soiIdx === -1) break;
      const eoiIdx = findMarker(buf, 0xFF, 0xD9, soiIdx + 2);
      if (eoiIdx === -1) break;
      lastFrameStart = soiIdx;
      lastFrameEnd = eoiIdx + 2;
      searchFrom = lastFrameEnd;
    }

    if (lastFrameStart !== -1) {
      const frame = buf.slice(lastFrameStart, lastFrameEnd);
      res.write(
        `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${frame.length}\r\n\r\n`
      );
      res.write(frame);
      res.write('\r\n');
      buf = buf.slice(lastFrameEnd);
    }

    // Cap buffer to avoid unbounded growth if no complete frame arrives
    if (buf.length > 1024 * 1024) buf = buf.slice(-256 * 1024);
  });

  child.on('error', (err) => {
    if (!res.writableEnded) res.end();
    console.error('Camera process error:', err.message);
  });
  child.stdout.on('end', () => { if (!res.writableEnded) res.end(); });
  res.on('close', () => { try { child.kill('SIGTERM'); } catch {} });
}

// ===========================================================================
// USB Camera Discovery
// ===========================================================================

function findUsbCamera() {
  // Scan all 64 possible video nodes — RPi5 reserves video2-9 for MIPI/ISP
  // so a USB UVC camera typically lands at video0, video1, or video10+.
  for (let i = 0; i < 64; i++) {
    try {
      const driverPath = `/sys/class/video4linux/video${i}/device/driver`;
      const link = fs.readlinkSync(driverPath);
      if (link.endsWith('uvcvideo')) return `/dev/video${i}`;
    } catch {}
  }
  return null;
}

// ===========================================================================
// Servo Control via FusionHat sysfs
// ===========================================================================

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
    fs.writeFileSync(`${pwmDir}/period`, String(SERVO_PERIOD_US));
    fs.writeFileSync(`${pwmDir}/enable`, '1');
    servoInitialized.add(channel);
  }

  fs.writeFileSync(`${pwmDir}/duty_cycle`, String(pulseUs));
}

// ===========================================================================
// LRF — M01 UART Protocol
// ===========================================================================

const UART_DEVICE       = '/dev/ttyAMA0';
const LASER_ON_CMD      = Buffer.from([0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1]);
const CONTINUOUS_CMD    = Buffer.from([0xAA, 0x00, 0x00, 0x21, 0x00, 0x01, 0x00, 0x00, 0x22]);
const LRF_RESTIM_MS     = 1500;   // re-send continuous command after this many ms idle

/**
 * Parse M01 frames from a buffer. Handles three formats matching C++ driver:
 *   0xEE  9-byte: BCD centimetres at bytes [5:6], × 10 = mm (warm-up/status frames)
 *   0xAA 13-byte: BCD mm at bytes [8:9] (full data frames, bytes[4]=0x00 bytes[5]=0x04)
 *   0xAA  9-byte: BCD mm at bytes [5:6] (compact data frames)
 *   0xAA  9-byte echo: bytes[4]=0x00 bytes[5]=0x01 — skip silently
 * Checksum = sum(bytes[1..N-2]) & 0xFF == bytes[N-1]
 * Returns { mm, consumed } for the first valid frame found, or null if none.
 */
function m01Checksum(buf, offset, len) {
  let ck = 0;
  for (let j = 1; j < len - 1; j++) ck = (ck + buf[offset + j]) & 0xFF;
  return ck;
}

function parseM01Frame(buf) {
  for (let i = 0; i < buf.length; i++) {
    const sync = buf[i];
    if (sync !== 0xAA && sync !== 0xEE) continue;

    const remaining = buf.length - i;
    if (remaining < 9) break;  // need at least 9 bytes

    // 0xEE frames — always 9 bytes, BCD centimetres at [5:6]
    if (sync === 0xEE) {
      if (m01Checksum(buf, i, 9) === buf[i + 8]) {
        const dh = buf[i + 5], dl = buf[i + 6];
        const cm = ((dh >> 4) & 0xF) * 1000 + (dh & 0xF) * 100 +
                   ((dl >> 4) & 0xF) * 10   + (dl & 0xF);
        return { mm: cm * 10, consumed: i + 9 };
      }
      return { mm: null, consumed: i + 1 };  // invalid — skip sync byte
    }

    // 0xAA — try 13-byte first if enough data
    if (remaining >= 13) {
      if (m01Checksum(buf, i, 13) === buf[i + 12] &&
          buf[i + 4] === 0x00 && buf[i + 5] === 0x04) {
        const dh = buf[i + 8], dl = buf[i + 9];
        const mm = ((dh >> 4) & 0xF) * 1000 + (dh & 0xF) * 100 +
                   ((dl >> 4) & 0xF) * 10   + (dl & 0xF);
        return { mm, consumed: i + 13 };
      }
    }

    // 0xAA 9-byte echo — skip silently
    if (buf[i + 4] === 0x00 && buf[i + 5] === 0x01) {
      return { mm: null, consumed: i + 9 };
    }

    // 0xAA 9-byte data frame
    if (m01Checksum(buf, i, 9) === buf[i + 8] &&
        buf[i + 4] === 0x00 && buf[i + 5] !== 0x00) {
      const dh = buf[i + 5], dl = buf[i + 6];
      const mm = ((dh >> 4) & 0xF) * 1000 + (dh & 0xF) * 100 +
                 ((dl >> 4) & 0xF) * 10   + (dl & 0xF);
      return { mm, consumed: i + 9 };
    }

    // Invalid 0xAA frame — skip sync byte
    return { mm: null, consumed: i + 1 };
  }
  return null;
}

// LRF session (one per server process — only one calibration client at a time)
const lrfSession = {
  fd: null,
  socket: null,    // net.Socket wrapping the fd
  restimTimer: null,
  lastFrameAt: 0,
};

function lrfSendCmd() {
  if (lrfSession.fd !== null) {
    try {
      fs.writeSync(lrfSession.fd, CONTINUOUS_CMD);
      lrfSession.lastFrameAt = Date.now();
    } catch (e) {
      console.error('LRF write error:', e.message);
    }
  }
}

function lrfOpen() {
  if (lrfSession.fd !== null) return;  // already open
  try {
    execSync(`stty -F ${UART_DEVICE} 9600 cs8 -cstopb -parenb raw -echo -echoe -echok`);

    // Open write-only fd for sending commands to LRF
    const fd = fs.openSync(UART_DEVICE, fs.constants.O_WRONLY | fs.constants.O_NOCTTY);
    lrfSession.fd = fd;

    // Spawn cat for reading — avoids TTY fd type restrictions in net.Socket/createReadStream
    const catProc = spawn('cat', [UART_DEVICE]);
    lrfSession.socket = catProc;  // stored for lrfClose()

    let rxBuf = Buffer.alloc(0);

    catProc.stdout.on('data', (chunk) => {
      lrfSession.lastFrameAt = Date.now();
      rxBuf = Buffer.concat([rxBuf, chunk]);
      if (rxBuf.length > 256) rxBuf = rxBuf.slice(-128);  // cap

      let result;
      while ((result = parseM01Frame(rxBuf)) !== null) {
        rxBuf = rxBuf.slice(result.consumed);
        if (result.mm === null) continue;  // echo / invalid — silently consumed
        // Update shared state so main HUD shows live range
        state.lrf_range_m = +(result.mm / 1000).toFixed(2);
        state.lrf_last_ts = Date.now();
        if (!state._lrf_logged) {
          console.log(`[LRF] First reading: ${state.lrf_range_m}m`);
          state._lrf_logged = true;
        }
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

    catProc.on('error', (err) => {
      console.error('LRF cat error:', err.message);
      lrfClose();
    });

    catProc.on('exit', () => {
      lrfClose();
    });

    // Send laser-on then continuous command (per M01 FAQ: laser must be enabled first)
    try { fs.writeSync(lrfSession.fd, LASER_ON_CMD); } catch (e) { /* best-effort */ }
    setTimeout(() => {
      try { fs.writeSync(lrfSession.fd, LASER_ON_CMD); } catch (e) { /* best-effort */ }
    }, 50);
    setTimeout(() => { lrfSendCmd(); }, 150);
    // Start re-stimulation timer
    lrfSession.restimTimer = setInterval(() => {
      if (Date.now() - lrfSession.lastFrameAt >= LRF_RESTIM_MS) {
        lrfSendCmd();
      }
    }, 500);

    console.log(`[LRF] Opened ${UART_DEVICE}`);

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
  if (lrfSession.restimTimer) {
    clearInterval(lrfSession.restimTimer);
    lrfSession.restimTimer = null;
  }
  if (lrfSession.socket) {
    try { lrfSession.socket.kill ? lrfSession.socket.kill() : (lrfSession.socket.destroy ? lrfSession.socket.destroy() : lrfSession.socket.close()); } catch {}
    lrfSession.socket = null;
  }
  if (lrfSession.fd !== null) {
    try { fs.closeSync(lrfSession.fd); } catch {}
    lrfSession.fd = null;
  }
}

// ===========================================================================
// Calibration Data Storage
// ===========================================================================

const CALIBRATION_FILE = path.join(__dirname, '..', 'config', 'calibration.json');

// In-memory calibration state
const calibrationData = {
  saved_at: null,
  servo: {
    pan_channel: SERVO_PAN_CH,
    tilt_channel: SERVO_TILT_CH,
    pan_center_deg: 90.0,
    tilt_center_deg: 90.0,
  },
  lrf: {
    pan_at_center_deg: 90.0,
    tilt_at_center_deg: 90.0,
    distance_at_center_mm: null,
  },
  cameras: null,  // { pixel_offset_x, pixel_offset_y, sample_count }
};

function saveCalibrationToFile() {
  const configDir = path.dirname(CALIBRATION_FILE);
  if (!fs.existsSync(configDir)) {
    fs.mkdirSync(configDir, { recursive: true });
  }
  calibrationData.saved_at = new Date().toISOString();
  fs.writeFileSync(CALIBRATION_FILE, JSON.stringify(calibrationData, null, 2));
  return CALIBRATION_FILE;
}

// ===========================================================================
// Static File Server
// ===========================================================================

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.json': 'application/json',
  '.ico':  'image/x-icon',
};

function securityHeaders() {
  return {
    'Content-Security-Policy':
      "default-src 'self'; connect-src ws: wss: http: https:; font-src https://fonts.googleapis.com https://fonts.gstatic.com; style-src 'self' https://fonts.googleapis.com 'unsafe-inline'; script-src 'self' 'unsafe-inline' https://fonts.googleapis.com",
    'X-Frame-Options': 'DENY',
    'X-Content-Type-Options': 'nosniff',
    'X-XSS-Protection': '1; mode=block',
  };
}

function serveStatic(req, res) {
  let urlPath = req.url === '/' ? '/index.html' : req.url;
  if (urlPath === '/calibrate') urlPath = '/calibrate.html';  // Route alias
  // Strip query string
  urlPath = urlPath.split('?')[0];
  const filePath = path.join(STATIC_ROOT, urlPath);

  // Security: ensure we stay inside STATIC_ROOT
  if (!filePath.startsWith(STATIC_ROOT)) {
    res.writeHead(403, securityHeaders());
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, securityHeaders());
      res.end('Not found: ' + urlPath);
      return;
    }
    const ext = path.extname(filePath);
    res.writeHead(200, {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      ...securityHeaders(),
    });
    res.end(data);
  });
}

// ===========================================================================
// Animated Mock State (for HUD when not connected to real hardware)
// ===========================================================================

const state = {
  mode: 'AUTO',
  fcs_state: 'TRACKING',
  frame_count: 12847,
  // Track blob wanders in a Lissajous pattern
  track_t: 0,
  // Gimbal position (absolute, accumulated from freecam commands)
  gimbalYaw: 12.4,
  gimbalPitch: -3.2,
  // p_hit oscillates
  phit_t: 0,
  // Health
  cpu_temp: 67.3,
  cpu_pct: 34.2,
  deadline_misses: 0,
  // HUD socket state
  hud_socket_connected: false,
  use_hud_socket: true,   // Always enabled — launched together with C++ binary
  // LRF — live range from UART (null = no reading yet)
  lrf_range_m: null,
  lrf_last_ts: 0,
};

function buildTelemetry() {
  state.track_t += 0.012;
  state.phit_t   += 0.021;
  state.frame_count += Math.round(120 * TELEMETRY_INTERVAL_MS / 1000);

  // Blob wanders in Lissajous
  const cx = CANVAS_W / 2 + Math.sin(state.track_t * 1.3) * 300;
  const cy = CANVAS_H / 2 + Math.sin(state.track_t * 0.9) * 180;

  // Gimbal uses accumulated position (not drifting sine wave)
  const yaw   = state.gimbalYaw;
  const pitch = state.gimbalPitch;

  // p_hit oscillates between 0.55 and 0.95
  const p_hit = 0.75 + Math.sin(state.phit_t) * 0.2;

  // Velocity from derivative of position
  const vx = Math.cos(state.track_t * 1.3) * 300 * 1.3 * 0.012;
  const vy = Math.cos(state.track_t * 0.9) * 180 * 0.9 * 0.012;

  // Ballistic lead proportional to velocity
  const az_lead_mrad = vx * 0.004;
  const el_lead_mrad = vy * 0.004;

  // CPU temp wanders
  state.cpu_temp = 67.3 + Math.sin(state.track_t * 0.1) * 3.0;
  state.cpu_pct  = 34.2 + Math.sin(state.track_t * 0.15) * 5.0;

  return {
    ts: Date.now(),
    mode: state.mode,
    fcs_state: state.fcs_state,
    frame_count: state.frame_count,
    gimbal: { yaw: +yaw.toFixed(2), pitch: +pitch.toFixed(2) },
    track: {
      valid: state.fcs_state === 'TRACKING' || state.fcs_state === 'ARMED',
      cx: +cx.toFixed(1),
      cy: +cy.toFixed(1),
      w: 120,
      h: 80,
      confidence: +(0.87 + Math.sin(state.phit_t * 1.3) * 0.08).toFixed(3),
      range_m: state.lrf_range_m !== null ? state.lrf_range_m : +(245.3 + Math.sin(state.track_t * 0.2) * 15).toFixed(1),
      vx: +vx.toFixed(3),
      vy: +vy.toFixed(3),
    },
    ballistic: {
      az_lead_mrad: +az_lead_mrad.toFixed(3),
      el_lead_mrad: +el_lead_mrad.toFixed(3),
      p_hit: +p_hit.toFixed(3),
    },
    health: {
      cpu_temp: +state.cpu_temp.toFixed(1),
      cpu_pct:  +state.cpu_pct.toFixed(1),
      deadline_misses: state.deadline_misses,
    },
  };
}

// ===========================================================================
// HUD Socket Client (UNIX domain socket)
// ===========================================================================

let hudSocket = null;
let hudSocketBuffer = '';
let hudSocketReconnectTimer = null;
let lastHudFrameData = null;  // Cache last valid frame for WebSocket clients

function connectHudSocket() {
  if (!state.use_hud_socket) {
    return;
  }

  if (hudSocket !== null) {
    return;
  }

  console.log(`[HUD Socket] Attempting to connect to ${HUD_SOCKET_PATH}...`);

  hudSocket = net.createConnection({ path: HUD_SOCKET_PATH }, () => {
    console.log('[HUD Socket] Connected');
    state.hud_socket_connected = true;
    hudSocketBuffer = '';
  });

  hudSocket.on('data', (data) => {
    // Append data to buffer and process newline-delimited JSON
    hudSocketBuffer += data.toString();

    let lines = hudSocketBuffer.split('\n');
    // Keep the last incomplete line in the buffer
    hudSocketBuffer = lines.pop();

    for (const line of lines) {
      if (!line.trim()) {
        continue;
      }

      try {
        const hudData = JSON.parse(line);
        lastHudFrameData = mapHudFrameToTelemetry(hudData);
      } catch (err) {
        console.warn('[HUD Socket] Failed to parse JSON:', line.slice(0, 100), err.message);
      }
    }
  });

  hudSocket.on('error', (err) => {
    console.error('[HUD Socket] Error:', err.message);
    state.hud_socket_connected = false;
    hudSocket = null;
    scheduleHudSocketReconnect();
  });

  hudSocket.on('close', () => {
    console.log('[HUD Socket] Disconnected');
    state.hud_socket_connected = false;
    hudSocket = null;
    scheduleHudSocketReconnect();
  });
}

function scheduleHudSocketReconnect() {
  if (hudSocketReconnectTimer) {
    return;
  }

  hudSocketReconnectTimer = setTimeout(() => {
    hudSocketReconnectTimer = null;
    connectHudSocket();
  }, HUD_RECONNECT_MS);
}

/**
 * Map HudFrame (from C++ socket) to frontend telemetry schema
 */
function mapHudFrameToTelemetry(hudData) {
  const stateName = FCS_STATES[hudData.state] || 'UNKNOWN';

  // Derive mode from actual FCS state reported by C++ binary
  const derivedMode = stateName === 'FREECAM' ? 'FREECAM'
    : (stateName === 'SEARCH' || stateName === 'TRACKING' || stateName === 'ARMED') ? 'AUTO'
    : 'IDLE_SAFE';
  state.mode = derivedMode;

  return {
    ts: Date.now(),
    mode: derivedMode,
    fcs_state: stateName,
    frame_count: state.frame_count,
    gimbal: {
      yaw: +(hudData.az || 0).toFixed(2),
      pitch: +(hudData.el || 0).toFixed(2),
    },
    track: {
      valid: stateName === 'TRACKING' || stateName === 'ARMED',
      cx: +(hudData.cx || 0).toFixed(1),
      cy: +(hudData.cy || 0).toFixed(1),
      w: +(hudData.w || 0).toFixed(1),
      h: +(hudData.h || 0).toFixed(1),
      confidence: +(hudData.conf || 0).toFixed(3),
      range_m: state.lrf_range_m !== null ? state.lrf_range_m : +(hudData.range || 0).toFixed(1),
      vx: +(hudData.vx || 0).toFixed(3),
      vy: +(hudData.vy || 0).toFixed(3),
    },
    ballistic: {
      az_lead_mrad: +(hudData.az_lead_mrad || 0).toFixed(3),
      el_lead_mrad: +(hudData.el_lead_mrad || 0).toFixed(3),
      p_hit: +(hudData.p_hit || 0).toFixed(3),
    },
    health: {
      cpu_temp: +(state.cpu_temp).toFixed(1),
      cpu_pct: +(state.cpu_pct).toFixed(1),
      deadline_misses: hudData.deadline_misses || 0,
    },
  };
}

// Start HUD socket connection on startup
connectHudSocket();

// Auto-start LRF on boot (stale reading cleared after 5s of silence)
setTimeout(() => {
  lrfOpen();
  setInterval(() => {
    if (state.lrf_last_ts > 0 && Date.now() - state.lrf_last_ts > 5000) {
      state.lrf_range_m = null;  // clear stale reading
    }
  }, 2000);
}, 500);

// ===========================================================================
// Command Socket Client (Node.js → C++ binary)
// ===========================================================================

let cmdSocket = null;
let cmdSocketReconnectTimer = null;

function sendCmd(line) {
  if (cmdSocket && !cmdSocket.destroyed) {
    cmdSocket.write(line + '\n');
  }
}

function connectCmdSocket() {
  if (cmdSocket !== null) return;

  cmdSocket = net.createConnection({ path: CMD_SOCKET_PATH }, () => {
    console.log('[CMD Socket] Connected to C++ binary');
  });

  cmdSocket.on('error', (err) => {
    console.warn('[CMD Socket] Error:', err.message);
    cmdSocket = null;
    scheduleCmdSocketReconnect();
  });

  cmdSocket.on('close', () => {
    console.log('[CMD Socket] Disconnected — will retry');
    cmdSocket = null;
    scheduleCmdSocketReconnect();
  });
}

function scheduleCmdSocketReconnect() {
  if (cmdSocketReconnectTimer) return;
  cmdSocketReconnectTimer = setTimeout(() => {
    cmdSocketReconnectTimer = null;
    connectCmdSocket();
  }, CMD_RECONNECT_MS);
}

// Start command socket connection on startup (with short delay for C++ binary to be ready)
setTimeout(connectCmdSocket, 1000);

// ===========================================================================
// MIPI preview from aurore binary via UNIX socket
// Protocol: [4-byte BE uint32 length][JPEG bytes] repeated
// ===========================================================================

const MIPI_SOCKET_PATH = '/run/aurore/mjpeg_stream.sock';
const USB_SOCKET_PATH  = '/run/aurore/mjpeg_usb_stream.sock';
const MIPI_RECONNECT_MS = 2000;

let mipiLatestFrame = null;
const mipiClients = new Set();
let mipiFrameWatchdog = null;

let usbLatestFrame = null;
const usbClients = new Set();

function dropMipiClients(reason) {
  if (mipiClients.size > 0) {
    console.log(`[MIPI] ${reason} — closing ${mipiClients.size} browser client(s)`);
    for (const r of mipiClients) { try { r.end(); } catch (_) {} }
    mipiClients.clear();
  }
}

function resetMipiWatchdog() {
  if (mipiFrameWatchdog) clearTimeout(mipiFrameWatchdog);
  mipiFrameWatchdog = setTimeout(() => {
    mipiFrameWatchdog = null;
    console.warn('[MIPI] No frame for 3s — dropping browser clients');
    dropMipiClients('frame timeout');
  }, 3000);
}

function dropUsbClients(reason) {
  if (usbClients.size > 0) {
    console.log(`[USB] ${reason} — closing ${usbClients.size} browser client(s)`);
    for (const r of usbClients) { try { r.end(); } catch (_) {} }
    usbClients.clear();
  }
}

let mipiSocket = null;
let mipiReconnectTimer = null;

function broadcastMipiFrame(jpeg) {
  mipiLatestFrame = jpeg;
  const header = `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`;
  for (const r of mipiClients) {
    if (!r.writableEnded) { r.write(header); r.write(jpeg); r.write('\r\n'); }
  }
  resetMipiWatchdog();
}

function connectMipiSocket() {
  if (mipiSocket) return;

  mipiSocket = net.createConnection({ path: MIPI_SOCKET_PATH }, () => {
    console.log('[MIPI] Connected to aurore MJPEG stream socket');
  });

  let rxBuf = Buffer.alloc(0);

  mipiSocket.on('data', (chunk) => {
    rxBuf = Buffer.concat([rxBuf, chunk]);

    // Parse length-prefixed JPEG frames
    while (rxBuf.length >= 4) {
      const frameLen = (rxBuf[0] << 24) | (rxBuf[1] << 16) | (rxBuf[2] << 8) | rxBuf[3];
      if (rxBuf.length < 4 + frameLen) break;  // incomplete frame
      const jpeg = rxBuf.slice(4, 4 + frameLen);
      rxBuf = rxBuf.slice(4 + frameLen);
      broadcastMipiFrame(jpeg);
    }

    // Guard against corrupt stream inflating the buffer
    if (rxBuf.length > 2 * 1024 * 1024) rxBuf = rxBuf.slice(-256 * 1024);
  });

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
}

function scheduleMipiReconnect() {
  if (mipiReconnectTimer) return;
  mipiReconnectTimer = setTimeout(() => {
    mipiReconnectTimer = null;
    connectMipiSocket();
  }, MIPI_RECONNECT_MS);
}

// Start connection with a short delay (aurore binary may not have the socket up yet)
setTimeout(connectMipiSocket, 1500);

// ---- USB camera socket (same length-prefix protocol) ----
let usbSocket = null;
let usbReconnectTimer = null;

function connectUsbSocket() {
  if (usbSocket) return;

  usbSocket = net.createConnection({ path: USB_SOCKET_PATH }, () => {
    console.log('[USB] Connected to aurore USB stream socket');
  });

  let rxBuf = Buffer.alloc(0);

  usbSocket.on('data', (chunk) => {
    rxBuf = Buffer.concat([rxBuf, chunk]);
    while (rxBuf.length >= 4) {
      const frameLen = (rxBuf[0] << 24) | (rxBuf[1] << 16) | (rxBuf[2] << 8) | rxBuf[3];
      if (rxBuf.length < 4 + frameLen) break;
      const jpeg = rxBuf.slice(4, 4 + frameLen);
      rxBuf = rxBuf.slice(4 + frameLen);
      usbLatestFrame = jpeg;
      const header = `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`;
      for (const r of usbClients) {
        if (!r.writableEnded) { r.write(header); r.write(jpeg); r.write('\r\n'); }
      }
    }
    if (rxBuf.length > 2 * 1024 * 1024) rxBuf = rxBuf.slice(-256 * 1024);
  });

  usbSocket.on('error', (err) => {
    console.warn('[USB] Socket error:', err.message);
    usbSocket = null;
    dropUsbClients('socket error');
    if (!usbReconnectTimer) usbReconnectTimer = setTimeout(() => { usbReconnectTimer = null; connectUsbSocket(); }, MIPI_RECONNECT_MS);
  });

  usbSocket.on('close', () => {
    usbSocket = null;
    dropUsbClients('socket closed');
    if (!usbReconnectTimer) usbReconnectTimer = setTimeout(() => { usbReconnectTimer = null; connectUsbSocket(); }, MIPI_RECONNECT_MS);
  });
}

setTimeout(connectUsbSocket, 1500);

// ===========================================================================
// WebSocket Servers (split by URL)
// ===========================================================================

const hudWss  = new WebSocketServer({ noServer: true });
const calibWss = new WebSocketServer({ noServer: true });

const server = http.createServer((req, res) => {
  // MJPEG stream endpoints
  if (req.url === '/stream/mipi') {
    res.writeHead(200, {
      'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
      'Cache-Control': 'no-cache, no-store',
      'Connection': 'keep-alive',
      'Access-Control-Allow-Origin': '*',
    });
    mipiClients.add(res);
    if (mipiLatestFrame) {
      res.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${mipiLatestFrame.length}\r\n\r\n`);
      res.write(mipiLatestFrame);
      res.write('\r\n');
    }
    req.on('close', () => mipiClients.delete(res));
    return;
  }

  if (req.url === '/stream/usb') {
    res.writeHead(200, {
      'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
      'Cache-Control': 'no-cache, no-store',
      'Connection': 'keep-alive',
      'Access-Control-Allow-Origin': '*',
    });
    usbClients.add(res);
    if (usbLatestFrame) {
      res.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${usbLatestFrame.length}\r\n\r\n`);
      res.write(usbLatestFrame);
      res.write('\r\n');
    }
    req.on('close', () => usbClients.delete(res));
    return;
  }

  // Servo control endpoints
  if (req.method === 'POST' && req.url === '/api/servo/center') {
    try {
      writeServoAngle(SERVO_PAN_CH,  90);
      writeServoAngle(SERVO_TILT_CH, 90);
      servoState.pan  = 90;
      servoState.tilt = 90;

      // Broadcast to calibration clients
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

      res.writeHead(200, { 
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
      });
      res.end(JSON.stringify({ ok: true, pan_deg: 90, tilt_deg: 90 }));
    } catch (err) {
      res.writeHead(500, { 
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
      });
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
        if (typeof pan_deg !== 'number' || typeof tilt_deg !== 'number') {
          throw new Error('pan_deg and tilt_deg required');
        }
        const panClamped  = Math.max(0, Math.min(180, pan_deg));
        const tiltClamped = Math.max(0, Math.min(180, tilt_deg));
        writeServoAngle(SERVO_PAN_CH,  panClamped);
        writeServoAngle(SERVO_TILT_CH, tiltClamped);
        servoState.pan  = panClamped;
        servoState.tilt = tiltClamped;

        // Broadcast to calibration clients
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

        res.writeHead(200, { 
          'Content-Type': 'application/json',
          'Access-Control-Allow-Origin': '*',
        });
        res.end(JSON.stringify({ ok: true, pan_deg: panClamped, tilt_deg: tiltClamped }));
      } catch (err) {
        res.writeHead(400, { 
          'Content-Type': 'application/json',
          'Access-Control-Allow-Origin': '*',
        });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }

  // Calibration save endpoint
  if (req.method === 'POST' && req.url === '/api/calibration/save') {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        
        // Update calibration data from request
        if (data.servo) {
          calibrationData.servo.pan_center_deg = data.servo.pan_center_deg;
          calibrationData.servo.tilt_center_deg = data.servo.tilt_center_deg;
        }
        if (data.lrf) {
          calibrationData.lrf.pan_at_center_deg = data.lrf.pan_at_center_deg;
          calibrationData.lrf.tilt_at_center_deg = data.lrf.tilt_at_center_deg;
          calibrationData.lrf.distance_at_center_mm = data.lrf.distance_at_center_mm;
        }
        if (data.cameras) {
          calibrationData.cameras = data.cameras;
        }

        const filePath = saveCalibrationToFile();
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ 
          ok: true, 
          file: filePath,
          saved_at: calibrationData.saved_at 
        }));
      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }

  // Fall through to static file serving
  serveStatic(req, res);
});

// Handle WebSocket upgrade routing
server.on('upgrade', (req, socket, head) => {
  if (req.url === '/ws/calib') {
    calibWss.handleUpgrade(req, socket, head, (ws) => calibWss.emit('connection', ws, req));
  } else {
    hudWss.handleUpgrade(req, socket, head, (ws) => hudWss.emit('connection', ws, req));
  }
});

// ===========================================================================
// HUD WebSocket Server
// ===========================================================================

const hudClients = new Set();

hudWss.on('connection', (ws, req) => {
  const addr = req.socket.remoteAddress;
  console.log(`[WS-HUD] Client connected: ${addr}`);
  hudClients.add(ws);

  ws.on('message', (raw) => {
    console.log('[WS-HUD] Received raw:', raw.toString().slice(0, 100));
    let cmd;
    try {
      cmd = JSON.parse(raw.toString());
      console.log('[WS-HUD] Parsed command:', cmd);
    } catch {
      console.warn('[WS-HUD] Bad JSON from client:', raw.toString().slice(0, 80));
      return;
    }

    switch (cmd.type) {
      case 'mode_switch':
        if (['AUTO', 'FREECAM', 'IDLE_SAFE'].includes(cmd.mode)) {
          state.mode = cmd.mode;
          if (cmd.mode === 'FREECAM') {
            state.fcs_state = 'FREECAM';
            sendCmd('MODE FREECAM');
          } else if (cmd.mode === 'AUTO') {
            state.fcs_state = 'SEARCH';
            sendCmd('MODE AUTO');
          } else {
            state.fcs_state = 'IDLE_SAFE';
            sendCmd('MODE IDLE');
          }
          console.log(`[CMD] mode_switch → ${cmd.mode}`);
        } else {
          console.warn(`[CMD] Unknown mode: ${cmd.mode}`);
        }
        break;

      case 'freecam': {
        const az = cmd.az;
        const el = cmd.el;

        if (typeof az !== 'number' || !isFinite(az) ||
            typeof el !== 'number' || !isFinite(el)) {
          console.warn(`[CMD] freecam invalid values: az=${az} el=${el}`);
          break;
        }

        state.gimbalYaw = az;
        state.gimbalPitch = el;

        state.gimbalYaw = Math.max(-90, Math.min(90, state.gimbalYaw));
        state.gimbalPitch = Math.max(-10, Math.min(45, state.gimbalPitch));

        // Write to actual servos - convert from -90..90 / -10..45 to 0..180
        // Note: Invert axes if servos are mounted backwards
        const INVERT_PAN = true;    // Set true if pan direction is reversed (A/D swapped)
        const INVERT_TILT = true;   // Set true if tilt direction is reversed (W moves down instead of up)
        
        const panAngle = (INVERT_PAN ? -state.gimbalYaw : state.gimbalYaw) + 90;
        const tiltAngle = (INVERT_TILT ? -state.gimbalPitch : state.gimbalPitch) + 90;
        
        try {
          writeServoAngle(SERVO_PAN_CH, panAngle);
          writeServoAngle(SERVO_TILT_CH, tiltAngle);
          console.log(`[SERVO] Wrote pan=${panAngle.toFixed(1)}° tilt=${tiltAngle.toFixed(1)}° (yaw=${state.gimbalYaw.toFixed(1)} pitch=${state.gimbalPitch.toFixed(1)})`);
        } catch (err) {
          console.error('[SERVO] Write failed:', err.message);
        }

        sendCmd(`FREECAM ${state.gimbalYaw.toFixed(3)} ${state.gimbalPitch.toFixed(3)}`);
        console.log(`[CMD] freecam az=${az.toFixed(2)} el=${el.toFixed(2)} → yaw=${state.gimbalYaw.toFixed(2)} pitch=${state.gimbalPitch.toFixed(2)}`);
        break;
      }

      default:
        console.warn(`[CMD] Unknown command type: ${cmd.type}`);
    }
  });

  ws.on('close', () => {
    console.log(`[WS-HUD] Client disconnected: ${addr}`);
    hudClients.delete(ws);
  });

  ws.on('error', (err) => {
    console.error(`[WS-HUD] Error from ${addr}:`, err.message);
    hudClients.delete(ws);
  });

  ws.send(JSON.stringify(buildTelemetry()));
});

// ===========================================================================
// Calibration WebSocket Server
// ===========================================================================

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

// ===========================================================================
// Broadcast Loop (HUD telemetry)
// ===========================================================================

setInterval(() => {
  if (hudClients.size === 0) return;

  let telemetry;
  if (state.use_hud_socket && state.hud_socket_connected && lastHudFrameData) {
    telemetry = lastHudFrameData;
  } else {
    state.frame_count += Math.round(120 * TELEMETRY_INTERVAL_MS / 1000);
    state.track_t += 0.012;
    state.phit_t += 0.021;
    telemetry = buildTelemetry();
  }

  const msg = JSON.stringify(telemetry);
  for (const ws of hudClients) {
    if (ws.readyState === ws.OPEN) {
      ws.send(msg);
    }
  }
}, TELEMETRY_INTERVAL_MS);

// ===========================================================================
// Graceful Shutdown
// ===========================================================================

process.on('SIGINT', () => {
  console.log('\n[Server] Shutting down gracefully...');
  if (hudSocket) {
    hudSocket.destroy();
  }
  if (hudSocketReconnectTimer) {
    clearTimeout(hudSocketReconnectTimer);
  }
  if (cmdSocket) {
    cmdSocket.destroy();
  }
  if (cmdSocketReconnectTimer) {
    clearTimeout(cmdSocketReconnectTimer);
  }
  lrfClose();
  server.close(() => {
    console.log('[Server] Closed');
    process.exit(0);
  });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`Aurore server running at http://0.0.0.0:${PORT}`);
  console.log(`WebSocket endpoints:`);
  console.log(`  - HUD: ws://localhost:${PORT}/ws`);
  console.log(`  - Calibration: ws://localhost:${PORT}/ws/calib`);
  console.log(`MJPEG streams:`);
  console.log(`  - MIPI: http://localhost:${PORT}/stream/mipi`);
  console.log(`  - USB:  http://localhost:${PORT}/stream/usb`);
  if (state.use_hud_socket) {
    console.log(`HUD socket: attempting connection to ${HUD_SOCKET_PATH} (reconnect interval: ${HUD_RECONNECT_MS}ms)`);
  } else {
    console.log(`HUD socket: disabled (using mock data generator)`);
  }
  console.log('Press Ctrl+C to stop.');
});
