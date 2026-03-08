/**
 * Aurore MkVII mock server
 * Serves static files from this directory + WebSocket telemetry on port 8080.
 *
 * Usage: node mock-server.js
 */

'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const net = require('net');
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

// ---------------------------------------------------------------------------
// Static file server
// ---------------------------------------------------------------------------

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

const server = http.createServer((req, res) => {
  serveStatic(req, res);
});

// ---------------------------------------------------------------------------
// Animated mock state
// ---------------------------------------------------------------------------

const state = {
  mode: 'AUTO',
  fcs_state: 'TRACKING',
  frame_count: 12847,
  // Track blob wanders in a Lissajous pattern
  track_t: 0,
  // Gimbal drifts slowly
  gimbal_t: 0,
  // p_hit oscillates
  phit_t: 0,
  // Health
  cpu_temp: 67.3,
  cpu_pct: 34.2,
  deadline_misses: 0,
  // HUD socket state
  hud_socket_connected: false,
  use_hud_socket: true,  // Set to false to disable HUD socket and use mock data only
};

function buildTelemetry() {
  state.track_t += 0.012;
  state.gimbal_t += 0.004;
  state.phit_t   += 0.021;
  state.frame_count += Math.round(120 * TELEMETRY_INTERVAL_MS / 1000);

  // Blob wanders in Lissajous
  const cx = CANVAS_W / 2 + Math.sin(state.track_t * 1.3) * 300;
  const cy = CANVAS_H / 2 + Math.sin(state.track_t * 0.9) * 180;

  // Gimbal drifts
  const yaw   = 12.4 + Math.sin(state.gimbal_t) * 5.0;
  const pitch = -3.2 + Math.cos(state.gimbal_t * 0.7) * 2.0;

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

// ---------------------------------------------------------------------------
// HUD Socket client (connects to C++ UNIX domain socket at /tmp/aurore_hud.sock)
// ---------------------------------------------------------------------------

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
 * HudFrame fields:
 *   state (0-6: BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED, FAULT)
 *   az_deg, el_deg (gimbal angles)
 *   target_cx, target_cy (track centroid in pixels)
 *   target_w, target_h (target bbox dimensions)
 *   velocity_x, velocity_y (target velocity, m/s or px/frame)
 *   confidence (track confidence 0-1)
 *   az_lead_mrad, el_lead_mrad (ballistics lead angles in mrad)
 *   p_hit (ballistics probability of hit)
 *   range_m (estimated range in meters)
 *   deadline_misses (count of missed deadlines)
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

// ---------------------------------------------------------------------------
// WebSocket server (upgrade from same HTTP server)
// ---------------------------------------------------------------------------

const wss = new WebSocketServer({ server, path: '/ws' });
const clients = new Set();

wss.on('connection', (ws, req) => {
  const addr = req.socket.remoteAddress;
  console.log(`[WS] Client connected: ${addr}`);
  clients.add(ws);

  ws.on('message', (raw) => {
    let cmd;
    try {
      cmd = JSON.parse(raw.toString());
    } catch {
      console.warn('[WS] Bad JSON from client:', raw.toString().slice(0, 80));
      return;
    }

    switch (cmd.type) {
      case 'mode_switch':
        if (['AUTO', 'FREECAM'].includes(cmd.mode)) {
          state.mode = cmd.mode;
          // Transition FCS state to reflect mode
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
        if (
          typeof az !== 'number' || !isFinite(az) ||
          typeof el !== 'number' || !isFinite(el) ||
          az < -180 || az > 180 ||
          el < -90  || el > 90
        ) {
          console.warn(`[CMD] freecam validation failed: az=${az} el=${el} — discarding`);
          break;
        }
        console.log(`[CMD] freecam az=${az.toFixed(2)} el=${el.toFixed(2)}`);
        state.gimbal_t = az * 0.05;
        break;
      }

      default:
        console.warn(`[CMD] Unknown command type: ${cmd.type}`);
    }
  });

  ws.on('close', () => {
    console.log(`[WS] Client disconnected: ${addr}`);
    clients.delete(ws);
  });

  ws.on('error', (err) => {
    console.error(`[WS] Error from ${addr}:`, err.message);
    clients.delete(ws);
  });

  // Send initial state immediately
  ws.send(JSON.stringify(buildTelemetry()));
});

// ---------------------------------------------------------------------------
// Broadcast loop
// ---------------------------------------------------------------------------

setInterval(() => {
  if (clients.size === 0) return;

  // Use HUD socket data if available and connected, otherwise fall back to mock
  let telemetry;
  if (state.use_hud_socket && state.hud_socket_connected && lastHudFrameData) {
    telemetry = lastHudFrameData;
  } else {
    // Update mock data
    state.frame_count += Math.round(120 * TELEMETRY_INTERVAL_MS / 1000);
    state.track_t += 0.012;
    state.gimbal_t += 0.004;
    state.phit_t += 0.021;
    telemetry = buildTelemetry();
  }

  const msg = JSON.stringify(telemetry);
  for (const ws of clients) {
    if (ws.readyState === ws.OPEN) {
      ws.send(msg);
    }
  }
}, TELEMETRY_INTERVAL_MS);

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\n[Server] Shutting down gracefully...');
  if (hudSocket) {
    hudSocket.destroy();
  }
  if (hudSocketReconnectTimer) {
    clearTimeout(hudSocketReconnectTimer);
  }
  server.close(() => {
    console.log('[Server] Closed');
    process.exit(0);
  });
});

server.listen(PORT, () => {
  console.log(`Aurore mock server running at http://localhost:${PORT}`);
  console.log(`WebSocket endpoint: ws://localhost:${PORT}/ws`);
  if (state.use_hud_socket) {
    console.log(`HUD socket: attempting connection to ${HUD_SOCKET_PATH} (reconnect interval: ${HUD_RECONNECT_MS}ms)`);
  } else {
    console.log(`HUD socket: disabled (using mock data generator)`);
  }
  console.log('Press Ctrl+C to stop.');
});
