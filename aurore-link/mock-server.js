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
const { WebSocketServer } = require('ws');

const PORT = 8080;
const TELEMETRY_INTERVAL_MS = 150;
const STATIC_ROOT = __dirname;

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
};

const FCS_STATES = ['BOOT', 'IDLE_SAFE', 'FREECAM', 'SEARCH', 'TRACKING', 'ARMED', 'FAULT'];
const CANVAS_W = 1536;
const CANVAS_H = 864;

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
  const msg = JSON.stringify(buildTelemetry());
  for (const ws of clients) {
    if (ws.readyState === ws.OPEN) {
      ws.send(msg);
    }
  }
}, TELEMETRY_INTERVAL_MS);

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------

server.listen(PORT, () => {
  console.log(`Aurore mock server running at http://localhost:${PORT}`);
  console.log(`WebSocket endpoint: ws://localhost:${PORT}/ws`);
  console.log('Press Ctrl+C to stop.');
});
