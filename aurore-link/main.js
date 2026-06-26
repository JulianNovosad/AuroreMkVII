/**
 * Aurore MkVII — Remote Control Station
 * AC-130 Military HUD Aesthetic
 * main.js: WebSocket client, real camera stream, HUD updates
 */

'use strict';

// ---------------------------------------------------------------------------
// DOM refs — Video Stream
// ---------------------------------------------------------------------------
const videoEl = document.getElementById('video');
const faultOverlay = document.getElementById('fault-overlay');
const faultReasonEl = document.getElementById('fault-reason');
const faultDetailEl = document.getElementById('fault-detail');

// ---------------------------------------------------------------------------
// DOM refs — HUD Overlay Quadrants
// ---------------------------------------------------------------------------
const fcsStateEl     = document.getElementById('fcs-state');
const phitEl         = document.getElementById('phit');
const modeIndEl      = document.getElementById('mode-ind');
const timestampEl    = document.getElementById('timestamp');
const sensorTempEl   = document.getElementById('sensor-temp');
const sensorGainEl   = document.getElementById('sensor-gain');
const rangeEl        = document.getElementById('range');
const trkConfEl      = document.getElementById('trk-conf');
const altitudeEl     = document.getElementById('altitude');
const lnkStatusEl    = document.getElementById('lnk-status');
const gimbalCoordsEl = document.getElementById('gimbal-coords');
const sensorParamsEl = document.getElementById('sensor-params');
const gimbalNeedle   = document.getElementById('gimbal-needle');

// ---------------------------------------------------------------------------
// DOM refs — Reticle / Pipper / Brackets
// ---------------------------------------------------------------------------
const pipperLead = document.getElementById('hud-pipper-lead');
const bracketTL  = document.querySelector('.bracket-tl');
const bracketTR  = document.querySelector('.bracket-tr');
const bracketBL  = document.querySelector('.bracket-bl');
const bracketBR  = document.querySelector('.bracket-br');

// ---------------------------------------------------------------------------
// Gimbal Control — Trapezoidal Velocity Profile
// ---------------------------------------------------------------------------

// Gimbal limits (MUST match C++: src/actuation/gimbal_controller.hpp)
const GIMBAL_YAW_MIN   = -90;
const GIMBAL_YAW_MAX   =  90;
const GIMBAL_PITCH_MIN = -10;
const GIMBAL_PITCH_MAX =  45;

// Trapezoid parameters
const MAX_SPEED = 60;          // °/s  — peak slew rate
const ACCEL     = 90;          // °/s² — ramp up / ramp down
const UPDATE_HZ = 30;
const dt        = 1 / UPDATE_HZ;

// Gimbal state
let yaw      = 0;   // current absolute position (°)
let pitch    = 0;
let velYaw   = 0;   // current velocity (°/s)
let velPitch = 0;

// WASD held-key state
const keyDown = { KeyW: false, KeyA: false, KeyS: false, KeyD: false };

// ---------------------------------------------------------------------------
// HUD Update Functions
// ---------------------------------------------------------------------------

// Track state for FAULT blink
let currentFcsState = 'BOOT';
let faultBlinkState = false;

// FAULT blink — 3s cycle (1.5s on, 1.5s off)
setInterval(() => {
  faultBlinkState = !faultBlinkState;
  if (currentFcsState === 'FAULT') {
    fcsStateEl.style.opacity = faultBlinkState ? 1.0 : 0.3;
  } else {
    fcsStateEl.style.opacity = 1.0;
  }
}, 1500);

function updateFcsState(state) {
  currentFcsState = state;
  fcsStateEl.textContent = 'SYS: ' + state;
  // Reset opacity when state changes away from FAULT
  if (state !== 'FAULT') {
    fcsStateEl.style.opacity = 1.0;
  }
  triggerGlitch(); // Visual feedback on state change
}

/**
 * Trigger glitch effect on state/mode change
 * Duration: 200ms (matches CSS animation)
 */
function triggerGlitch() {
  const hudOverlay = document.querySelector('.hud-overlay');
  hudOverlay.classList.add('glitch-active');
  
  setTimeout(() => {
    hudOverlay.classList.remove('glitch-active');
  }, 200);
}

function updateGimbalPipper(gimbal) {
  // Offset crosshair shows gimbal pointing position relative to camera center
  // Gimbal yaw/pitch in degrees; convert to pixel offset
  const videoEl = document.getElementById('video');
  const W = videoEl.clientWidth;
  const H = videoEl.clientHeight;
  const cx = W / 2;
  const cy = H / 2;

  // RPI Cam Module 3: 66° horizontal FOV, 41° vertical FOV
  const degScaleX = W / 66;  // pixels per degree horizontal
  const degScaleY = H / 41;  // pixels per degree vertical

  const displayYaw   = yaw;
  const displayPitch = pitch;

  // Gimbal yaw moves horizontally, pitch moves vertically
  const px = cx + displayYaw * degScaleX;
  const py = cy - displayPitch * degScaleY; // pitch up = negative y

  // Position pipper (40×40 SVG, center at 20,20)
  pipperLead.style.left = (px - 20) + 'px';
  pipperLead.style.top  = (py - 20) + 'px';
}

function updateTrackBrackets(track) {
  if (!track || !track.valid) {
    // Hide all brackets
    bracketTL.style.display = 'none';
    bracketTR.style.display = 'none';
    bracketBL.style.display = 'none';
    bracketBR.style.display = 'none';
    bracketsVisible = false;
    return;
  }

  const videoEl = document.getElementById('video');
  const W = videoEl.clientWidth;
  const H = videoEl.clientHeight;
  
  // Detect lock transition (brackets transitioning from hidden to visible)
  if (!bracketsVisible) {
    triggerGlitch(); // Glitch feedback on new lock
    bracketsVisible = true;
  }

  // Scale track coordinates (from 1536x864 to video element size)
  const tx = (track.cx / 1536) * W;
  const ty = (track.cy / 864) * H;
  const tw = (track.w / 1536) * W;
  const th = (track.h / 864) * H;


  const halfW = tw / 2;
  const halfH = th / 2;
  const bracketSize = 20; // SVG bracket size

  // Top-Left bracket (position at corner, bracket extends inward)
  bracketTL.style.display = 'block';
  bracketTL.style.left = (tx - halfW) + 'px';
  bracketTL.style.top  = (ty - halfH) + 'px';
  bracketTL.classList.add('locked'); // Add locked state color


  // Top-Right bracket
  bracketTR.style.display = 'block';
  bracketTR.style.left = (tx + halfW - bracketSize) + 'px';
  bracketTR.style.top  = (ty - halfH) + 'px';
  bracketTR.classList.add('locked');
  
  // Bottom-Left bracket
  bracketBL.style.display = 'block';
  bracketBL.style.left = (tx - halfW) + 'px';
  bracketBL.style.top  = (ty + halfH - bracketSize) + 'px';
  bracketBL.classList.add('locked');
  
  // Bottom-Right bracket
  bracketBR.style.display = 'block';
  bracketBR.style.left = (tx + halfW - bracketSize) + 'px';
  bracketBR.style.top  = (ty + halfH - bracketSize) + 'px';
  bracketBR.classList.add('locked');
}

function updateGimbalDial(yaw) {
  // Rotate needle based on yaw (0° = up/N, clockwise positive)
  gimbalNeedle.style.transform = `rotate(${yaw}deg)`;
}

function updateHUD(s) {
  // FCS State
  updateFcsState(s.fcs_state);
  
  // P_hit
  const pct = (s.ballistic.p_hit * 100).toFixed(0);
  phitEl.textContent = 'PHIT ' + pct;
  
  // Mode indicator
  if (s.mode !== previousMode) {
    triggerGlitch(); // Mode changed, trigger visual feedback
    previousMode = s.mode;
  }
  currentMode = s.mode; // Sync with server state
  const modeActive = s.mode === 'AUTO' ? '[X]' : '[ ]';
  modeIndEl.textContent = modeActive + ' AUTO';
  
  // Timestamp (HH:MM:SS)
  const date = new Date(s.ts);
  const hh = String(date.getUTCHours()).padStart(2, '0');
  const mm = String(date.getUTCMinutes()).padStart(2, '0');
  const ss = String(date.getUTCSeconds()).padStart(2, '0');
  timestampEl.textContent = `${hh}:${mm}:${ss}`;
  
  // Sensor temp / gain (mapped from health data)
  sensorTempEl.textContent = 'WHT ' + s.health.cpu_temp.toFixed(0) + 'C';
  sensorGainEl.textContent = 'GAIN ' + s.health.cpu_pct.toFixed(0);
  
  // Range / Track confidence
  if (s.track && s.track.valid) {
    rangeEl.textContent = 'RNG ' + s.track.range_m.toFixed(0) + 'M';
    const confPct = (s.track.confidence * 100).toFixed(0);
    trkConfEl.textContent = 'TRK ' + confPct;

    // Update track brackets
    updateTrackBrackets(s.track);
  } else {
    rangeEl.textContent = 'RNG ---M';
    trkConfEl.textContent = 'TRK --';
    updateTrackBrackets({ valid: false });
  }

  // Update gimbal pipper (offset crosshair shows gimbal position)
  // Always update from telemetry as fallback, smoothing loop also updates when running
  updateGimbalPipper(s.gimbal);

  // Altitude (placeholder — not in current telemetry)
  altitudeEl.textContent = 'ALT ---M';

  // Link status (derived from WebSocket state)
  // Updated separately by connection handler

  gimbalCoordsEl.textContent = `AZ ${yaw.toFixed(1)}° EL ${pitch.toFixed(1)}°`;
  gimbalNeedle.style.transform = `rotate(${yaw}deg)`;

  // Sensor params (placeholder — not in current telemetry)
  sensorParamsEl.textContent = 'WHOT BRT -- CNT --';
}

// ---------------------------------------------------------------------------
// WebSocket client
// ---------------------------------------------------------------------------

let ws = null;
let reconnectDelay = 2000;

function updateLinkStatus(connected) {
  lnkStatusEl.textContent = connected ? 'LNK: UP' : 'LNK: DOWN';
}

function showFaultOverlay(reason, detail) {
  faultReasonEl.textContent = reason || 'RPi 5 not responding';
  faultDetailEl.textContent = detail || 'Cannot connect to Aurore MkVII control system';
  faultOverlay.classList.remove('hidden');
}

function hideFaultOverlay() {
  faultOverlay.classList.add('hidden');
}

function connect() {
  updateLinkStatus(false);
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const host = window.location.host || 'localhost:8080';
  const wsUrl = `${protocol}//${host}/ws`;
  ws = new WebSocket(wsUrl);

  ws.addEventListener('open', () => {
    updateLinkStatus(true);
    reconnectDelay = 2000;
  });

  ws.addEventListener('message', (ev) => {
    try {
      const s = JSON.parse(ev.data);
      if (s.fault) {
        showFaultOverlay(s.fault_reason, s.fault_detail);
      } else {
        hideFaultOverlay();
        updateHUD(s);
      }
    } catch (err) {
      console.warn('Bad telemetry JSON:', err);
      showFaultOverlay('Protocol Error', 'Failed to parse telemetry');
    }
  });

  ws.addEventListener('close', () => {
    updateLinkStatus(false);
    showFaultOverlay('Connection Lost', 'WebSocket disconnected from control station');
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 1.5, 10000);
  });

  ws.addEventListener('error', () => {
    // close event will follow
  });
}

function sendCmd(cmd) {
  console.log('[WS] sendCmd called:', cmd, 'ws:', ws ? 'exists' : 'null', 'readyState:', ws ? ws.readyState : 'N/A');
  if (ws && ws.readyState === WebSocket.OPEN) {
    console.log('[WS] Sending:', JSON.stringify(cmd));
    ws.send(JSON.stringify(cmd));
  } else {
    console.error('[WS] Cannot send - WebSocket not open. readyState:', ws ? ws.readyState : 'null');
  }
}

// ---------------------------------------------------------------------------
// Controls — virtual joystick (invisible but functional)
// ---------------------------------------------------------------------------

const joystick = document.getElementById('joystick');
const joyDot   = document.getElementById('joy-dot');
const joyReadout = document.getElementById('joy-readout');
const modeToggleBtn = document.getElementById('mode-toggle-btn');

// Mode toggle button handler
if (modeToggleBtn) {
  console.log('[MODE] Button found, adding click handler');
  modeToggleBtn.addEventListener('click', () => {
    console.log('[MODE] Button clicked, currentMode:', currentMode);
    const newMode = currentMode === 'AUTO' ? 'FREECAM' : 'AUTO';
    switchMode(newMode);
  });
} else {
  console.warn('[MODE] Button NOT found in DOM');
}

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
  joyReadout.textContent = 'AZ ' + joyAz.toFixed(1) + '° EL ' + joyEl.toFixed(1) + '°';
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
  joyInterval = setInterval(() => {
    if (joyActive && currentMode === 'FREECAM') {
      sendCmd({ type: 'freecam', az: joyAz, el: joyEl });
    }
  }, 100);
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
  joyInterval = setInterval(() => {
    if (joyActive && currentMode === 'FREECAM') {
      sendCmd({ type: 'freecam', az: joyAz, el: joyEl });
    }
  }, 100);
}, { passive: false });
window.addEventListener('touchmove', (e) => {
  if (!joyActive) return;
  const t = e.touches[0];
  updateJoyDot(t.clientX, t.clientY);
}, { passive: true });
window.addEventListener('touchend', () => { if (joyActive) joyReset(); });

// ---------------------------------------------------------------------------
// Keyboard Controls Implementation
// ---------------------------------------------------------------------------

// Notification system
let notificationTimeout = null;

function showNotification(message, duration = 4000) {
  // Create notification element if it doesn't exist
  let notif = document.getElementById('keyboard-notification');
  if (!notif) {
    notif = document.createElement('div');
    notif.id = 'keyboard-notification';
    notif.style.cssText = `
      position: absolute;
      bottom: 100px;
      left: 50%;
      transform: translateX(-50%);
      background: rgba(0, 0, 0, 0.8);
      border: 1px solid #ffffff;
      color: #ffffff;
      padding: 12px 24px;
      font-family: 'Share Tech Mono', monospace;
      font-size: 18px;
      z-index: 200;
      pointer-events: none;
      opacity: 1;
      transition: opacity 0.5s ease-out;
    `;
    document.getElementById('video-area').appendChild(notif);
  }
  
  notif.textContent = message;
  notif.style.opacity = '1';
  
  // Clear existing timeout
  if (notificationTimeout) {
    clearTimeout(notificationTimeout);
  }
  
  // Fade out after duration
  notificationTimeout = setTimeout(() => {
    notif.style.opacity = '0';
  }, duration);
}

// Mode switching
function switchMode(newMode) {
  if (newMode === 'AUTO' || newMode === 'FREECAM') {
    currentMode = newMode; // Update local state immediately
    sendCmd({ type: 'mode_switch', mode: newMode });
    
    // Update mode indicator
    const modeInd = document.getElementById('mode-ind');
    const modeBtn = document.getElementById('mode-toggle-btn');
    
    if (newMode === 'FREECAM') {
      if (modeInd) modeInd.textContent = '[X] FREECAM';
      if (modeBtn) {
        modeBtn.textContent = 'MODE: FREECAM (Press M)';
        modeBtn.classList.add('active');
      }
    } else {
      if (modeInd) modeInd.textContent = '[ ] AUTO';
      if (modeBtn) {
        modeBtn.textContent = 'MODE: AUTO (Press M)';
        modeBtn.classList.remove('active');
      }
    }
    
    showNotification(`MODE: ${newMode} — ${newMode === 'AUTO' ? 'Click to target' : 'WASD to slew, R to center'}`);
  }
}

// ---------------------------------------------------------------------------
// Trapezoidal Velocity Profile — 30 Hz control tick
// ---------------------------------------------------------------------------

function moveToward(val, target, step) {
  if (val < target) return Math.min(val + step, target);
  if (val > target) return Math.max(val - step, target);
  return val;
}

function trapezoidTick() {
  // Desired velocity from held keys
  let wantVelYaw   = 0;
  let wantVelPitch = 0;
  if (keyDown.KeyA) wantVelYaw   -= MAX_SPEED;
  if (keyDown.KeyD) wantVelYaw   += MAX_SPEED;
  if (keyDown.KeyW) wantVelPitch += MAX_SPEED;
  if (keyDown.KeyS) wantVelPitch -= MAX_SPEED;

  // Ramp velocity toward desired (trapezoid — constant acceleration)
  velYaw   = moveToward(velYaw,   wantVelYaw,   ACCEL * dt);
  velPitch = moveToward(velPitch, wantVelPitch, ACCEL * dt);

  // Integrate position
  yaw   = Math.max(GIMBAL_YAW_MIN,   Math.min(GIMBAL_YAW_MAX,   yaw   + velYaw   * dt));
  pitch = Math.max(GIMBAL_PITCH_MIN, Math.min(GIMBAL_PITCH_MAX, pitch + velPitch * dt));

  // Kill velocity at limits to prevent windup
  if (yaw   <= GIMBAL_YAW_MIN   || yaw   >= GIMBAL_YAW_MAX)   velYaw   = 0;
  if (pitch <= GIMBAL_PITCH_MIN || pitch >= GIMBAL_PITCH_MAX) velPitch = 0;

  // Send absolute target to C++ (joystick bypasses this when active)
  if (currentMode === 'FREECAM' && !joyActive) {
    sendCmd({ type: 'freecam', az: yaw, el: pitch });
  }
}

setInterval(trapezoidTick, 1000 / UPDATE_HZ);

// WASD handlers — just gate on FREECAM, trapezoid does the rest
function onKeyDown(key) {
  if (currentMode !== 'FREECAM') return;
  keyDown[key] = true;
}

function onKeyUp(key) {
  keyDown[key] = false;
}

// Zoom control
let currentZoom = 1.0;

function adjustZoom(delta) {
  currentZoom = Math.max(0.5, Math.min(3.0, currentZoom + delta));
  showNotification(`ZOOM: ${(currentZoom * 100).toFixed(0)}%`);
  videoEl.style.transform = currentZoom === 1.0 ? '' : `scale(${currentZoom})`;
  videoEl.style.transformOrigin = 'center center';
}

// Target assignment
function assignTargetAtPosition(screenX, screenY) {
  const rect = videoEl.getBoundingClientRect();
  
  // Calculate click position relative to center of screen
  const centerX = rect.left + rect.width / 2;
  const centerY = rect.top + rect.height / 2;
  const offsetX = screenX - centerX;  // Positive = right, Negative = left
  const offsetY = screenY - centerY;  // Positive = down, Negative = up
  
  // RPI Cam Module 3: 66° horizontal FOV, 41° vertical FOV
  // Convert pixel offset to degrees
  const yawDelta   =  (offsetX / (rect.width  / 2)) * 33;    // ±33° (half of 66°)
  const pitchDelta = -(offsetY / (rect.height / 2)) * 20.5;  // ±20.5° (half of 41°)

  // Click offset is relative to where the gimbal is currently pointing.
  const absoluteYaw   = Math.max(GIMBAL_YAW_MIN,   Math.min(GIMBAL_YAW_MAX,   yaw   + yawDelta));
  const absolutePitch = Math.max(GIMBAL_PITCH_MIN,  Math.min(GIMBAL_PITCH_MAX, pitch + pitchDelta));

  console.log('[CLICK] Screen:', { x: screenX, y: screenY }, 'Offset:', { x: offsetX.toFixed(0), y: offsetY.toFixed(0) });
  console.log('[CLICK] Angles:', { yawDelta: yawDelta.toFixed(1), pitchDelta: pitchDelta.toFixed(1), absoluteYaw: absoluteYaw.toFixed(1), absolutePitch: absolutePitch.toFixed(1) });

  yaw = absoluteYaw;
  pitch = absolutePitch;
  velYaw = 0;
  velPitch = 0;

  showNotification(`TARGET: AZ ${absoluteYaw.toFixed(1)}° EL ${absolutePitch.toFixed(1)}°`);
}

function clearTarget() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'mode_switch', mode: 'AUTO' }));
  }
  showNotification('TARGET: CLEARED');
}

// Global keyboard event listener
console.log('[KEYBOARD] Adding keydown listener');
window.addEventListener('keydown', (e) => {
  console.log('[KEY] Key pressed:', e.code);
  // Prevent default for control keys
  if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyR', 'Digit1', 'Digit2', 'KeyM', 'Equal', 'Minus'].includes(e.code)) {
    e.preventDefault();
  }

  switch(e.code) {
    case 'Digit1':
      console.log('[MODE] Digit1 pressed, switching to AUTO');
      switchMode('AUTO');
      break;
    case 'Digit2':
      console.log('[MODE] Digit2 pressed, switching to FREECAM');
      switchMode('FREECAM');
      break;
    case 'KeyM':
      console.log('[MODE] KeyM pressed, toggling mode. currentMode:', currentMode);
      // Toggle between AUTO and FREECAM
      switchMode(currentMode === 'AUTO' ? 'FREECAM' : 'AUTO');
      break;
    case 'KeyW':
    case 'KeyA':
    case 'KeyS':
    case 'KeyD':
      onKeyDown(e.code);
      break;
    case 'KeyR':
      if (currentMode === 'FREECAM') {
        keyDown.KeyW = keyDown.KeyA = keyDown.KeyS = keyDown.KeyD = false;
        yaw = 0; pitch = 0; velYaw = 0; velPitch = 0;
        sendCmd({ type: 'freecam', az: 0, el: 0 });
        showNotification('GIMBAL: CENTERED');
      }
      break;
    case 'Equal':
    case 'NumpadAdd':
      adjustZoom(0.1);
      break;
    case 'Minus':
    case 'NumpadSubtract':
      adjustZoom(-0.1);
      break;
  }
});

window.addEventListener('keyup', (e) => {
  if (['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(e.code)) {
    onKeyUp(e.code);
  }
});

// Mouse wheel zoom
window.addEventListener('wheel', (e) => {
  e.preventDefault();
  const delta = e.deltaY > 0 ? -0.05 : 0.05;
  adjustZoom(delta);
}, { passive: false });

// Track current mode
let currentMode = 'AUTO';

// Click-to-target
videoEl.addEventListener('mousedown', (e) => {
  if (e.button === 0) { // Left click
    if (currentMode === 'AUTO') {
      assignTargetAtPosition(e.clientX, e.clientY);
    }
  } else if (e.button === 2) { // Right click
    e.preventDefault();
    clearTarget();
  }
});

// Block context menu on video
videoEl.addEventListener('contextmenu', (e) => {
  e.preventDefault();
});

let previousMode = 'AUTO';
let bracketsVisible = false; // Track if brackets are currently shown (locked)

// ---------------------------------------------------------------------------
// Camera stream toggle (MIPI ↔ USB)
// ---------------------------------------------------------------------------
let currentCam = 'mipi';
const camToggleBtn = document.getElementById('cam-toggle-btn');
if (camToggleBtn) {
  camToggleBtn.addEventListener('click', () => {
    if (currentCam === 'mipi') {
      currentCam = 'usb';
      videoEl.src = '/stream/usb';
      camToggleBtn.textContent = 'CAM: USB';
      camToggleBtn.classList.add('usb-active');
    } else {
      currentCam = 'mipi';
      videoEl.src = '/stream/mipi';
      camToggleBtn.textContent = 'CAM: MIPI';
      camToggleBtn.classList.remove('usb-active');
    }
  });
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
connect();
