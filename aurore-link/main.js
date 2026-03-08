/**
 * Aurore MkVII — Remote Control Station
 * main.js: WebSocket client, canvas animation, SVG HUD, telemetry panel, controls
 */

'use strict';

// ---------------------------------------------------------------------------
// DOM refs
// ---------------------------------------------------------------------------
const canvas  = document.getElementById('video');
const ctx     = canvas.getContext('2d');
const hudSvg  = document.getElementById('hud');
const sidebar = document.getElementById('sidebar');

// ---------------------------------------------------------------------------
// Canvas sizing — match display size for crisp render
// ---------------------------------------------------------------------------
const SCENE_W = 1536;
const SCENE_H = 864;

function resizeCanvas() {
  const area = document.getElementById('video-area');
  const rect = area.getBoundingClientRect();
  canvas.width  = rect.width;
  canvas.height = rect.height;
  hudSvg.setAttribute('viewBox', `0 0 ${rect.width} ${rect.height}`);
  hudSvg.setAttribute('width',  rect.width);
  hudSvg.setAttribute('height', rect.height);
}

window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// Scale from scene coords (1536×864) to canvas display coords
function scaleX(x) { return x * canvas.width  / SCENE_W; }
function scaleY(y) { return y * canvas.height / SCENE_H; }

// ---------------------------------------------------------------------------
// Canvas — fake thermal video
// ---------------------------------------------------------------------------

// Simple LCG noise
let _seed = 42;
function lcgRand() {
  _seed = (_seed * 1664525 + 1013904223) & 0xffffffff;
  return (_seed >>> 0) / 0xffffffff;
}

// Each "target" blob wanders on a Lissajous figure
const blobs = [
  { ax: 0.8, ay: 0.7, px: 0.5, py: 0.5, fx: 1.3, fy: 0.9, phase: 0 },
  { ax: 0.3, ay: 0.2, px: 0.2, py: 0.3, fx: 0.7, fy: 1.1, phase: 1.2 },
  { ax: 0.2, ay: 0.15, px: 0.75, py: 0.7, fx: 1.7, fy: 0.6, phase: 2.4 },
];

let blobT = 0;
let trackState = { valid: false, cx: 768, cy: 432 };

function drawThermal(timestamp) {
  const W = canvas.width;
  const H = canvas.height;
  blobT = timestamp * 0.0012;

  // Dark thermal background — redraw with fade for motion blur effect
  ctx.fillStyle = 'rgba(10, 12, 10, 0.85)';
  ctx.fillRect(0, 0, W, H);

  // Noise grain
  const imgData = ctx.createImageData(W, H);
  const d = imgData.data;
  for (let i = 0; i < d.length; i += 4) {
    const v = Math.floor(lcgRand() * 18);
    d[i] = v; d[i+1] = v + 2; d[i+2] = v; d[i+3] = 255;
  }
  ctx.putImageData(imgData, 0, 0);

  // Draw blobs
  blobs.forEach((b, idx) => {
    const t = blobT + b.phase;
    let bx, by;

    // If track is valid and this is the primary blob (idx===0), snap to track position
    if (idx === 0 && trackState.valid) {
      bx = scaleX(trackState.cx);
      by = scaleY(trackState.cy);
    } else {
      bx = (b.px + Math.sin(t * b.fx) * b.ax * 0.3) * W;
      by = (b.py + Math.sin(t * b.fy) * b.ay * 0.5) * H;
    }

    const radius = W * 0.04;
    const grad = ctx.createRadialGradient(bx, by, 0, bx, by, radius);
    grad.addColorStop(0,   'rgba(255, 255, 240, 0.95)');
    grad.addColorStop(0.3, 'rgba(200, 230, 200, 0.6)');
    grad.addColorStop(0.7, 'rgba(100, 160, 100, 0.2)');
    grad.addColorStop(1,   'rgba(0, 0, 0, 0)');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.ellipse(bx, by, radius, radius * 0.65, 0, 0, Math.PI * 2);
    ctx.fill();
  });
}

function animationLoop(ts) {
  drawThermal(ts);
  requestAnimationFrame(animationLoop);
}
requestAnimationFrame(animationLoop);

// ---------------------------------------------------------------------------
// SVG HUD — create elements once, update in-place
// ---------------------------------------------------------------------------

function svgEl(tag, attrs) {
  const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  return el;
}

// Crosshair
const chTop    = svgEl('line', { stroke: '#00ff41', 'stroke-width': 1, opacity: 0.7 });
const chBot    = svgEl('line', { stroke: '#00ff41', 'stroke-width': 1, opacity: 0.7 });
const chLeft   = svgEl('line', { stroke: '#00ff41', 'stroke-width': 1, opacity: 0.7 });
const chRight  = svgEl('line', { stroke: '#00ff41', 'stroke-width': 1, opacity: 0.7 });
const chCenter = svgEl('circle', { r: 3, fill: 'none', stroke: '#00ff41', 'stroke-width': 1, opacity: 0.7 });

// Track box
const trackBox  = svgEl('rect', { fill: 'none', stroke: '#00ff41', 'stroke-width': 1.5, display: 'none' });
// Velocity vector
const velLine   = svgEl('line', { stroke: '#ff9900', 'stroke-width': 1.5, display: 'none' });
// Confidence ring
const confRing  = svgEl('circle', { fill: 'none', stroke: '#00aaff', 'stroke-width': 2,
                                    'stroke-dasharray': '0 999', display: 'none' });
// Ballistic lead dot
const leadDot   = svgEl('circle', { r: 5, fill: 'none', stroke: '#ff3333', 'stroke-width': 1.5, display: 'none' });
const leadLine  = svgEl('line', { stroke: '#ff3333', 'stroke-width': 1, 'stroke-dasharray': '4 3', display: 'none' });
// P_hit label
const phitLabel = svgEl('text', { fill: '#ff3333', 'font-size': 11, 'font-family': 'monospace', display: 'none' });

[chTop, chBot, chLeft, chRight, chCenter,
 trackBox, velLine, confRing, leadDot, leadLine, phitLabel].forEach(el => hudSvg.appendChild(el));

function updateHUD(s) {
  const W = canvas.width;
  const H = canvas.height;
  const cx = W / 2;
  const cy = H / 2;
  const arm = 20;

  // Crosshair
  chTop.setAttribute('x1', cx); chTop.setAttribute('y1', cy - arm - 8);
  chTop.setAttribute('x2', cx); chTop.setAttribute('y2', cy - 8);
  chBot.setAttribute('x1', cx); chBot.setAttribute('y1', cy + 8);
  chBot.setAttribute('x2', cx); chBot.setAttribute('y2', cy + arm + 8);
  chLeft.setAttribute('x1', cx - arm - 8); chLeft.setAttribute('y1', cy);
  chLeft.setAttribute('x2', cx - 8);       chLeft.setAttribute('y2', cy);
  chRight.setAttribute('x1', cx + 8);      chRight.setAttribute('y1', cy);
  chRight.setAttribute('x2', cx + arm + 8); chRight.setAttribute('y2', cy);
  chCenter.setAttribute('cx', cx); chCenter.setAttribute('cy', cy);

  const track = s.track;
  const bal   = s.ballistic;

  if (track && track.valid) {
    const tx = scaleX(track.cx);
    const ty = scaleY(track.cy);
    const tw = scaleX(track.w);
    const th = scaleY(track.h);

    trackBox.setAttribute('x', tx - tw / 2);
    trackBox.setAttribute('y', ty - th / 2);
    trackBox.setAttribute('width',  tw);
    trackBox.setAttribute('height', th);
    trackBox.setAttribute('display', '');

    // Velocity vector — scale px per frame * display factor
    const vScale = 15;
    const vx2 = tx + track.vx * vScale;
    const vy2 = ty + track.vy * vScale;
    velLine.setAttribute('x1', tx); velLine.setAttribute('y1', ty);
    velLine.setAttribute('x2', vx2); velLine.setAttribute('y2', vy2);
    velLine.setAttribute('display', '');

    // Confidence ring — circumscribed circle around track box, dashed by confidence
    const r = Math.max(tw, th) * 0.6;
    const circ = 2 * Math.PI * r;
    const dash = circ * track.confidence;
    confRing.setAttribute('cx', tx); confRing.setAttribute('cy', ty);
    confRing.setAttribute('r', r);
    confRing.setAttribute('stroke-dasharray', `${dash.toFixed(1)} ${(circ - dash).toFixed(1)}`);
    confRing.setAttribute('display', '');

    // Ballistic lead dot — offset from crosshair by mrad * scale (1 mrad ≈ 8px at scene size)
    const mradScale = (W / SCENE_W) * 8;
    const ldx = cx + bal.az_lead_mrad * mradScale;
    const ldy = cy - bal.el_lead_mrad * mradScale; // el up = negative y
    leadDot.setAttribute('cx', ldx); leadDot.setAttribute('cy', ldy);
    leadDot.setAttribute('display', '');
    leadLine.setAttribute('x1', cx); leadLine.setAttribute('y1', cy);
    leadLine.setAttribute('x2', ldx); leadLine.setAttribute('y2', ldy);
    leadLine.setAttribute('display', '');
    phitLabel.setAttribute('x', ldx + 8); phitLabel.setAttribute('y', ldy - 8);
    phitLabel.textContent = `p=${(bal.p_hit * 100).toFixed(0)}%`;
    phitLabel.setAttribute('display', '');

    // Update blob snap
    trackState = { valid: true, cx: track.cx, cy: track.cy };
  } else {
    [trackBox, velLine, confRing, leadDot, leadLine, phitLabel].forEach(el =>
      el.setAttribute('display', 'none'));
    trackState.valid = false;
  }
}

// ---------------------------------------------------------------------------
// Telemetry panel
// ---------------------------------------------------------------------------

function tempClass(t) {
  if (t < 70) return 'temp-ok';
  if (t < 80) return 'temp-warm';
  return 'temp-hot';
}

const stateColors = {
  TRACKING: 'state-TRACKING', ARMED: 'state-ARMED', FREECAM: 'state-FREECAM',
  SEARCH: 'state-SEARCH', FAULT: 'state-FAULT', IDLE_SAFE: 'state-IDLE_SAFE', BOOT: 'state-BOOT',
};

function updatePanel(s) {
  // FCS state
  const stEl = document.getElementById('fcs-state');
  stEl.textContent = s.fcs_state;
  stEl.className = stateColors[s.fcs_state] || '';

  // Mode buttons
  document.getElementById('btn-auto').className  = 'mode-btn' + (s.mode === 'AUTO'    ? ' active-auto' : '');
  document.getElementById('btn-free').className  = 'mode-btn' + (s.mode === 'FREECAM' ? ' active-free' : '');

  // Gimbal
  document.getElementById('val-yaw').textContent   = s.gimbal.yaw.toFixed(2) + '°';
  document.getElementById('val-pitch').textContent = s.gimbal.pitch.toFixed(2) + '°';

  // Track
  if (s.track.valid) {
    document.getElementById('val-range').textContent = s.track.range_m.toFixed(0) + ' m';
    document.getElementById('val-conf').textContent  = (s.track.confidence * 100).toFixed(0) + '%';
  } else {
    document.getElementById('val-range').textContent = '—';
    document.getElementById('val-conf').textContent  = '—';
  }

  // Ballistic
  document.getElementById('val-az').textContent    = s.ballistic.az_lead_mrad.toFixed(2) + ' mrad';
  document.getElementById('val-el').textContent    = s.ballistic.el_lead_mrad.toFixed(2) + ' mrad';
  const pct = (s.ballistic.p_hit * 100).toFixed(0);
  document.getElementById('val-phit').textContent  = pct + '%';
  document.getElementById('bar-phit').style.width  = pct + '%';

  // Health
  const tEl = document.getElementById('val-temp');
  tEl.textContent = s.health.cpu_temp.toFixed(1) + '°C';
  tEl.className   = 'telem-value ' + tempClass(s.health.cpu_temp);
  document.getElementById('val-cpu').textContent  = s.health.cpu_pct.toFixed(1) + '%';
  document.getElementById('val-frames').textContent = s.health.deadline_misses;

  // Timestamp
  document.getElementById('ts-badge').textContent = new Date(s.ts).toISOString().slice(11, 23);
}

// ---------------------------------------------------------------------------
// Main render function
// ---------------------------------------------------------------------------

function render(s) {
  updateHUD(s);
  updatePanel(s);
}

// ---------------------------------------------------------------------------
// WebSocket client
// ---------------------------------------------------------------------------

let ws = null;
let reconnectDelay = 2000;

function setConnBadge(state) {
  const el = document.getElementById('conn-badge');
  el.className = state;
  el.textContent = state.toUpperCase();
}

function connect() {
  setConnBadge('reconnecting');
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const host = window.location.host || 'localhost:8080';
  const wsUrl = `${protocol}//${host}/ws`;
  ws = new WebSocket(wsUrl);

  ws.addEventListener('open', () => {
    setConnBadge('connected');
    reconnectDelay = 2000;
  });

  ws.addEventListener('message', (ev) => {
    try {
      const s = JSON.parse(ev.data);
      render(s);
    } catch (err) {
      console.warn('Bad telemetry JSON:', err);
    }
  });

  ws.addEventListener('close', () => {
    setConnBadge('disconnected');
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 1.5, 10000);
  });

  ws.addEventListener('error', () => {
    // close event will follow
  });
}

function sendCmd(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(cmd));
  }
}

// ---------------------------------------------------------------------------
// Controls — mode switch
// ---------------------------------------------------------------------------

document.getElementById('btn-auto').addEventListener('click', () => {
  sendCmd({ type: 'mode_switch', mode: 'AUTO' });
});
document.getElementById('btn-free').addEventListener('click', () => {
  sendCmd({ type: 'mode_switch', mode: 'FREECAM' });
});

// ---------------------------------------------------------------------------
// Controls — virtual joystick
// ---------------------------------------------------------------------------

const joystick = document.getElementById('joystick');
const joyDot   = document.getElementById('joy-dot');
const joyReadout = document.getElementById('joy-readout');

const JOY_MAX_AZ = 45;
const JOY_MAX_EL = 45;
let joyActive = false;
let joyOrigin = { x: 0, y: 0 };
let joyInterval = null;
let joyAz = 0, joyEl = 0;

function joyCenter() {
  const r = joystick.getBoundingClientRect();
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
}

function joyRadius() {
  return joystick.getBoundingClientRect().width / 2;
}

function updateJoyDot(clientX, clientY) {
  const c = joyCenter();
  const maxR = joyRadius() - 10;
  let dx = clientX - c.x;
  let dy = clientY - c.y;
  const dist = Math.hypot(dx, dy);
  if (dist > maxR) {
    dx = dx / dist * maxR;
    dy = dy / dist * maxR;
  }
  joyDot.style.left = (50 + (dx / maxR) * 50) + '%';
  joyDot.style.top  = (50 + (dy / maxR) * 50) + '%';

  joyAz = (dx / maxR) * JOY_MAX_AZ;
  joyEl = -(dy / maxR) * JOY_MAX_EL; // up = positive elevation
  joyReadout.textContent = `AZ ${joyAz.toFixed(1)}° EL ${joyEl.toFixed(1)}°`;
}

function joyReset() {
  joyActive = false;
  joystick.classList.remove('active');
  joyDot.style.left = '50%';
  joyDot.style.top  = '50%';
  joyReadout.textContent = 'AZ 0.0° EL 0.0°';
  clearInterval(joyInterval);
  joyInterval = null;
  joyAz = 0; joyEl = 0;
}

joystick.addEventListener('mousedown', (e) => {
  joyActive = true;
  joystick.classList.add('active');
  updateJoyDot(e.clientX, e.clientY);
  joyInterval = setInterval(() => sendCmd({ type: 'freecam', az: joyAz, el: joyEl }), 100);
});
window.addEventListener('mousemove', (e) => {
  if (!joyActive) return;
  updateJoyDot(e.clientX, e.clientY);
});
window.addEventListener('mouseup', () => { if (joyActive) joyReset(); });

joystick.addEventListener('touchstart', (e) => {
  e.preventDefault();
  joyActive = true;
  joystick.classList.add('active');
  const t = e.touches[0];
  updateJoyDot(t.clientX, t.clientY);
  joyInterval = setInterval(() => sendCmd({ type: 'freecam', az: joyAz, el: joyEl }), 100);
}, { passive: false });
window.addEventListener('touchmove', (e) => {
  if (!joyActive) return;
  const t = e.touches[0];
  updateJoyDot(t.clientX, t.clientY);
}, { passive: true });
window.addEventListener('touchend', () => { if (joyActive) joyReset(); });

// ---------------------------------------------------------------------------
// Phone view toggle
// ---------------------------------------------------------------------------

const app = document.getElementById('app');
document.getElementById('phone-toggle').addEventListener('click', () => {
  app.classList.toggle('phone-view');
  resizeCanvas();
});

// Auto-set phone view on narrow screens
const mq = window.matchMedia('(max-width: 640px)');
mq.addEventListener('change', (e) => {
  if (e.matches) app.classList.add('phone-view');
  else app.classList.remove('phone-view');
  resizeCanvas();
});
if (mq.matches) app.classList.add('phone-view');

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
connect();
