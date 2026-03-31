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
const HUD_SOCKET_PATH = '/tmp/aurore_hud.sock';
const HUD_RECONNECT_MS = 2000;

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

// ===========================================================================
// USB Camera Discovery
// ===========================================================================

function findUsbCamera() {
  for (let i = 0; i < 10; i++) {
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
    execSync(`stty -F ${UART_DEVICE} 9600 cs8 -cstopb -parenb raw -echo -echoe -echok`);
    const fd = fs.openSync(UART_DEVICE, fs.constants.O_RDWR | fs.constants.O_NOCTTY);
    lrfSession.fd = fd;

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
  if (lrfSession.restimTimer) {
    clearInterval(lrfSession.restimTimer);
    lrfSession.restimTimer = null;
  }
  if (lrfSession.socket) {
    try { lrfSession.socket.destroy(); } catch {}
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
      "default-src 'self'; connect-src ws: wss:; font-src https://fonts.googleapis.com https://fonts.gstatic.com; style-src 'self' https://fonts.googleapis.com 'unsafe-inline'",
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
  use_hud_socket: false,  // Disabled by default - enable only when C++ HUD socket is available
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
      range_m: +(245.3 + Math.sin(state.track_t * 0.2) * 15).toFixed(1),
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

  return {
    ts: Date.now(),
    mode: state.mode,
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
      range_m: +(hudData.range || 0).toFixed(1),
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

// ===========================================================================
// WebSocket Servers (split by URL)
// ===========================================================================

const hudWss  = new WebSocketServer({ noServer: true });
const calibWss = new WebSocketServer({ noServer: true });

const server = http.createServer((req, res) => {
  // MJPEG stream endpoints
  if (req.url === '/stream/mipi') {
    const child = spawn('libcamera-vid', [
      '--codec', 'mjpeg',
      '--width', '1536',
      '--height', '864',
      '--framerate', '60',
      '--nopreview',
      '--timeout', '0',
      '-o', '-',
    ], { stdio: ['ignore', 'pipe', 'pipe'] });

    child.on('error', (err) => {
      console.error('[MIPI] Spawn error:', err.message);
      if (!res.headersSent) {
        res.writeHead(503, { 'Content-Type': 'text/plain' });
        res.end('FAIL: libcamera-vid not available - install with: sudo apt install libcamera-apps');
      }
    });
    
    child.stderr.on('data', (data) => {
      console.error('[MIPI] libcamera-vid:', data.toString().trim());
    });

    pipeAsMjpeg(res, child);
    return;
  }

  if (req.url === '/stream/usb') {
    const usbDev = findUsbCamera();
    if (!usbDev) {
      res.writeHead(503, { 'Content-Type': 'text/plain' });
      res.end('FAIL: USB UVC camera not found on /dev/video0-9');
      return;
    }

    const child = spawn('ffmpeg', [
      '-f', 'v4l2',
      '-input_format', 'mjpeg',
      '-framerate', '60',
      '-video_size', '1280x720',
      '-i', usbDev,
      '-f', 'mjpeg',
      'pipe:1',
    ], { stdio: ['ignore', 'pipe', 'pipe'] });

    child.on('error', (err) => {
      console.error('[USB] Spawn error:', err.message);
      if (!res.headersSent) {
        res.writeHead(503, { 'Content-Type': 'text/plain' });
        res.end('FAIL: ffmpeg not available');
      }
    });
    
    child.stderr.on('data', (data) => {
      // FFmpeg outputs a lot, only log errors
      const str = data.toString();
      if (str.includes('Error') || str.includes('Invalid')) {
        console.error('[USB] ffmpeg:', str.trim());
      }
    });

    pipeAsMjpeg(res, child);
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

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, pan_deg: panClamped, tilt_deg: tiltClamped }));
      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
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
    let cmd;
    try {
      cmd = JSON.parse(raw.toString());
    } catch {
      console.warn('[WS-HUD] Bad JSON from client:', raw.toString().slice(0, 80));
      return;
    }

    switch (cmd.type) {
      case 'mode_switch':
        if (['AUTO', 'FREECAM'].includes(cmd.mode)) {
          state.mode = cmd.mode;
          if (cmd.mode === 'FREECAM') {
            state.fcs_state = 'FREECAM';
          } else {
            state.fcs_state = 'TRACKING';
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
