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
// Keyboard Controls State
// ---------------------------------------------------------------------------
const keyState = {};
const keyTimers = {};
const TAP_WINDOW_MS = 100;  // Double tap window for snappy response
const TAP_DELTA = 0.75;     // Single tap: 0.75°
const DOUBLE_TAP_DELTA = 3; // Double tap: 3°
const HOLD_DELTA = 0.15;    // Hold: 0.15° per 100ms (1.5°/sec)
const HOLD_INTERVAL_MS = 100; // 10Hz command rate for hold slew

// Accumulated gimbal position (matches C++ gimbal controller)
let accumulatedYaw = 0;
let accumulatedPitch = 0;

// Gimbal limits (MUST match C++: src/actuation/gimbal_controller.hpp)
const GIMBAL_YAW_MIN = -90;
const GIMBAL_YAW_MAX = 90;
const GIMBAL_PITCH_MIN = -10;
const GIMBAL_PITCH_MAX = 45;

// Smoothing for realistic gimbal movement (matches real servo dynamics)
let targetYaw = 0;             // Target gimbal position
let targetPitch = 0;
let currentYaw = 0;            // Current (smoothed) position
let currentPitch = 0;
let velocityYaw = 0;           // Current angular velocity (°/sec)
let velocityPitch = 0;

// Gimbal dynamics (MUST match real servo specs: 0.17s per 60°)
const MAX_ANGULAR_VELOCITY = 353;    // 60° per 0.17s = 353°/sec max
const MAX_ANGULAR_ACCELERATION = 2000; // 2000°/sec² for smooth accel/decel
const SMOOTHING_ENABLED = false;  // DISABLED FOR DEBUGGING - direct control

// Servo latency simulation (real servos have ~70ms delay)
const SERVO_LATENCY_MS = 70;
const SERVO_LATENCY_FRAMES = 4;  // ~70ms at 60fps
let latencyBufferYaw = [];       // Circular buffer for delayed position
let latencyBufferPitch = [];

// PD control state
let prevErrorYaw = 0;
let prevErrorPitch = 0;
let lastSendTime = 0;
let smoothingInitialized = false;  // Track if smoothing has started

let holdInterval = null;

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

  // Use smoothed current position if smoothing is active, otherwise use telemetry
  const displayYaw = (SMOOTHING_ENABLED && smoothingInitialized) ? currentYaw : gimbal.yaw;
  const displayPitch = (SMOOTHING_ENABLED && smoothingInitialized) ? currentPitch : gimbal.pitch;

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

  // Gimbal coords - use smoothed values when available
  if (SMOOTHING_ENABLED && smoothingInitialized) {
    gimbalCoordsEl.textContent = `AZ ${currentYaw.toFixed(1)}° EL ${currentPitch.toFixed(1)}°`;
  } else {
    gimbalCoordsEl.textContent = 'AZ ' + s.gimbal.yaw.toFixed(1) + '° EL ' + s.gimbal.pitch.toFixed(1) + '°';
  }

  // Update analog dial - use smoothed values when available
  if (SMOOTHING_ENABLED && smoothingInitialized) {
    gimbalNeedle.style.transform = `rotate(${currentYaw}deg)`;
  } else {
    updateGimbalDial(s.gimbal.yaw);
  }

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
      updateHUD(s);
    } catch (err) {
      console.warn('Bad telemetry JSON:', err);
    }
  });

  ws.addEventListener('close', () => {
    updateLinkStatus(false);
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

// Gimbal smoothing - called every animation frame
// Implements PD control + servo latency simulation
let lastSmoothTime = 0;
let commandFrameCount = 0;

function updateGimbalSmoothing(timestamp) {
  // Mark smoothing as initialized on first run
  if (!smoothingInitialized) {
    smoothingInitialized = true;
    console.log('[SMOOTH] Smoothing initialized, video:', videoEl ? videoEl.clientWidth + 'x' + videoEl.clientHeight : 'null');
    console.log('[SMOOTH] Starting loop, SMOOTHING_ENABLED:', SMOOTHING_ENABLED);
  }

  if (!SMOOTHING_ENABLED) {
    // No smoothing - just use target directly
    currentYaw = targetYaw;
    currentPitch = targetPitch;
    if (currentMode === 'FREECAM') {
      console.log('[SMOOTH] No smoothing, sending:', { az: currentYaw, el: currentPitch });
      sendCmd({ type: 'freecam', az: currentYaw, el: currentPitch });
    }
  } else {
    const dt = lastSmoothTime ? (timestamp - lastSmoothTime) / 1000 : 0;
    lastSmoothTime = timestamp;
    
    // Debug: log every 100 frames to avoid spam
    commandFrameCount++;
    if (commandFrameCount % 100 === 0) {
      console.log('[SMOOTH] Loop running, mode:', currentMode, 'targetYaw:', targetYaw.toFixed(2), 'targetPitch:', targetPitch.toFixed(2), 'accumulatedYaw:', accumulatedYaw.toFixed(2), 'accumulatedPitch:', accumulatedPitch.toFixed(2));
    }

    // Limit dt to prevent huge jumps after tab switch
    const deltaTime = Math.min(dt, 0.05); // Cap at 50ms
    
    // Calculate error to target
    const errorYaw = targetYaw - currentYaw;
    const errorPitch = targetPitch - currentPitch;
    
    // PD control with higher gains for smooth motion
    const Kp = 25;   // Proportional gain
    const Kd = 0.08; // Derivative gain (damping)
    
    // Calculate derivative (rate of change of error)
    const derivYaw = (errorYaw - prevErrorYaw) / deltaTime;
    const derivPitch = (errorPitch - prevErrorPitch) / deltaTime;
    
    prevErrorYaw = errorYaw;
    prevErrorPitch = errorPitch;
    
    // PD output = proportional + derivative
    let cmdVelocityYaw = Kp * errorYaw + Kd * derivYaw;
    let cmdVelocityPitch = Kp * errorPitch + Kd * derivPitch;
    
    // Apply acceleration limits for smoothness
    const maxAccelDt = MAX_ANGULAR_ACCELERATION * deltaTime;
    const accelYaw = cmdVelocityYaw - velocityYaw;
    const accelPitch = cmdVelocityPitch - velocityPitch;
    
    velocityYaw += Math.max(-maxAccelDt, Math.min(maxAccelDt, accelYaw));
    velocityPitch += Math.max(-maxAccelDt, Math.min(maxAccelDt, accelPitch));
    
    // Clamp velocity to max
    velocityYaw = Math.max(-MAX_ANGULAR_VELOCITY, Math.min(MAX_ANGULAR_VELOCITY, velocityYaw));
    velocityPitch = Math.max(-MAX_ANGULAR_VELOCITY, Math.min(MAX_ANGULAR_VELOCITY, velocityPitch));
    
    // Update position
    currentYaw += velocityYaw * deltaTime;
    currentPitch += velocityPitch * deltaTime;
    
    // Snap to target if very close
    if (Math.abs(errorYaw) < 0.01) currentYaw = targetYaw;
    if (Math.abs(errorPitch) < 0.01) currentPitch = targetPitch;
    
    // Add to latency buffer
    latencyBufferYaw.push({ yaw: currentYaw, pitch: currentPitch });
    latencyBufferPitch.push({ yaw: currentYaw, pitch: currentPitch });
    
    // Remove old entries (keep only last N frames)
    if (latencyBufferYaw.length > SERVO_LATENCY_FRAMES) {
      latencyBufferYaw.shift();
      latencyBufferPitch.shift();
    }
    
    // Send with latency: use buffered position from 70ms ago
    // If buffer not full yet, use current position
    const sendYaw = latencyBufferYaw.length > 0 ? latencyBufferYaw[0].yaw : currentYaw;
    const sendPitch = latencyBufferPitch.length > 0 ? latencyBufferPitch[0].pitch : currentPitch;
    
    // Debug: log buffer state every 50 frames
    if (commandFrameCount % 50 === 0) {
      console.log('[SMOOTH] Buffer state:', { 
        bufferLen: latencyBufferYaw.length, 
        sendYaw: sendYaw.toFixed(2), 
        sendPitch: sendPitch.toFixed(2),
        currentYaw: currentYaw.toFixed(2),
        currentPitch: currentPitch.toFixed(2),
        targetYaw: targetYaw.toFixed(2),
        targetPitch: targetPitch.toFixed(2)
      });
    }

    // Throttle sends to ~60Hz to reduce choppiness
    if (timestamp - lastSendTime >= 16.67) {
      // Only send if values are valid numbers AND we're in FREECAM mode
      if (currentMode === 'FREECAM' &&
          typeof sendYaw === 'number' && typeof sendPitch === 'number' &&
          isFinite(sendYaw) && isFinite(sendPitch)) {
        console.log('[SMOOTH] Sending freecam:', { az: sendYaw, el: sendPitch, mode: currentMode });
        sendCmd({ type: 'freecam', az: sendYaw, el: sendPitch });
        lastSendTime = timestamp;

        // Update pipper display directly from smoothing loop
        if (pipperLead && videoEl && videoEl.clientWidth > 0 && videoEl.clientHeight > 0) {
          const W = videoEl.clientWidth;
          const H = videoEl.clientHeight;
          const cx = W / 2;
          const cy = H / 2;
          const degScaleX = W / 66;
          const degScaleY = H / 41;
          const px = cx + sendYaw * degScaleX;
          const py = cy - sendPitch * degScaleY;
          pipperLead.style.left = (px - 20) + 'px';
          pipperLead.style.top  = (py - 20) + 'px';
          pipperLead.style.display = 'block';  // Ensure pipper is visible
          // Debug: log first few updates
          if (lastSendTime < 1000) {
            console.log('[PIP] Update:', sendYaw.toFixed(2), sendPitch.toFixed(2), '-> px:', px.toFixed(0), py.toFixed(0));
          }
        }

        // Update gimbal coords display
        if (gimbalCoordsEl) {
          gimbalCoordsEl.textContent = `AZ ${sendYaw.toFixed(1)}° EL ${sendPitch.toFixed(1)}°`;
        }

        // Update gimbal dial (analog needle)
        if (gimbalNeedle) {
          gimbalNeedle.style.transform = `rotate(${sendYaw}deg)`;
        }
      }
    }
  }
  
  // Continue animation loop
  requestAnimationFrame(updateGimbalSmoothing);
}

// Start smoothing loop
console.log('[SMOOTH] Checking if should start loop, SMOOTHING_ENABLED:', SMOOTHING_ENABLED);
if (SMOOTHING_ENABLED) {
  console.log('[SMOOTH] Starting animation loop');
  requestAnimationFrame(updateGimbalSmoothing);
}

// Gimbal control command
function sendGimbalCommand(azDelta, elDelta) {
  console.log('[GIMBAL] sendGimbalCommand called:', { azDelta, elDelta });
  // Accumulate deltas for absolute position
  accumulatedYaw += azDelta;
  accumulatedPitch += elDelta;
  console.log('[GIMBAL] After accumulate:', { accumulatedYaw, accumulatedPitch });

  // Clamp to gimbal limits (MUST match C++: src/actuation/gimbal_controller.hpp)
  accumulatedYaw = Math.max(GIMBAL_YAW_MIN, Math.min(GIMBAL_YAW_MAX, accumulatedYaw));
  accumulatedPitch = Math.max(GIMBAL_PITCH_MIN, Math.min(GIMBAL_PITCH_MAX, accumulatedPitch));

  // Set target for smoothing
  targetYaw = accumulatedYaw;
  targetPitch = accumulatedPitch;
  console.log('[GIMBAL] After set target:', { targetYaw, targetPitch });
  
  // If smoothing is disabled, send immediately
  if (!SMOOTHING_ENABLED) {
    console.log('[GIMBAL] Smoothing disabled, sending immediately:', { az: targetYaw, el: targetPitch });
    sendCmd({ type: 'freecam', az: targetYaw, el: targetPitch });
  }
}

// Key event handlers
function handleWASDKey(key, isPressed) {
  console.log('[WASD] handleWASDKey called:', key, isPressed, 'currentMode:', currentMode);
  if (currentMode !== 'FREECAM') {
    console.log('[WASD] Not in FREECAM mode, ignoring');
    return;
  }

  const now = Date.now();

  if (isPressed) {
    console.log('[WASD] Key pressed, stopping hold slew');
    // Stop any existing hold interval first
    stopHoldSlew();
    
    keyState[key] = true;
    const lastTap = keyTimers[key];
    
    if (lastTap && (now - lastTap < TAP_WINDOW_MS)) {
      // Double tap detected (within 500ms)
      keyTimers[key] = 0;
      applyGimbalDelta(key, DOUBLE_TAP_DELTA);
      showNotification(`${key}: +${DOUBLE_TAP_DELTA}°`);
    } else {
      // Single tap detected
      keyTimers[key] = now;
      applyGimbalDelta(key, TAP_DELTA);
      showNotification(`${key}: +${TAP_DELTA}°`);
      
      // Start hold slew after tap window if still pressing
      setTimeout(() => {
        if (keyState[key] && keyTimers[key] === now) {
          startHoldSlew(key);
        }
      }, TAP_WINDOW_MS);
    }
  } else {
    keyState[key] = false;
    stopHoldSlew();
  }
}

function applyGimbalDelta(key, delta) {
  console.log('[WASD] applyGimbalDelta:', key, delta);
  switch(key) {
    case 'KeyW': sendGimbalCommand(0, delta); break;      // Pitch up
    case 'KeyS': sendGimbalCommand(0, -delta); break;     // Pitch down
    case 'KeyA': sendGimbalCommand(-delta, 0); break;     // Yaw left
    case 'KeyD': sendGimbalCommand(delta, 0); break;      // Yaw right
  }
  console.log('[WASD] After sendGimbalCommand:', { targetYaw, targetPitch, accumulatedYaw, accumulatedPitch });
}

function startHoldSlew(key) {
  if (!keyState[key]) return;
  
  const deltaMap = {
    'KeyW': [0, HOLD_DELTA],
    'KeyS': [0, -HOLD_DELTA],
    'KeyA': [-HOLD_DELTA, 0],
    'KeyD': [HOLD_DELTA, 0]
  };
  
  const [azDelta, elDelta] = deltaMap[key];
  
  // Send commands at 10Hz while holding
  holdInterval = setInterval(() => {
    if (!keyState[key]) {
      stopHoldSlew();
      return;
    }
    sendGimbalCommand(azDelta, elDelta);
  }, HOLD_INTERVAL_MS);
}

function stopHoldSlew() {
  if (holdInterval) {
    clearInterval(holdInterval);
    holdInterval = null;
  }
}

// Zoom control
let currentZoom = 1.0;

function adjustZoom(delta) {
  currentZoom = Math.max(0.5, Math.min(3.0, currentZoom + delta));
  showNotification(`ZOOM: ${(currentZoom * 100).toFixed(0)}%`);
  // TODO: Apply zoom to canvas/video when zoom feature is implemented
}

// Target assignment
function assignTargetAtPosition(screenX, screenY) {
  const rect = videoEl.getBoundingClientRect();
  const scaleX = 1536 / rect.width;
  const scaleY = 864 / rect.height;

  const frameX = (screenX - rect.left) * scaleX;
  const frameY = (screenY - rect.top) * scaleY;

  // Convert pixel position to gimbal angles
  // Center of screen = (0°, 0°)
  // RPI Cam Module 3: 66° horizontal FOV, 41° vertical FOV
  const centerX = rect.width / 2;
  const centerY = rect.height / 2;
  const offsetX = screenX - centerX;
  const offsetY = screenY - centerY;
  
  // Calculate gimbal angles based on click position
  const yaw = (offsetX / centerX) * 33;  // ±33° (half of 66° horizontal FOV)
  const pitch = -(offsetY / centerY) * 20.5;  // ±20.5° (half of 41° vertical FOV), negative because Y is inverted
  
  // Clamp to gimbal limits
  const clampedYaw = Math.max(GIMBAL_YAW_MIN, Math.min(GIMBAL_YAW_MAX, yaw));
  const clampedPitch = Math.max(GIMBAL_PITCH_MIN, Math.min(GIMBAL_PITCH_MAX, pitch));

  // Update accumulated position and target for smoothing
  accumulatedYaw = clampedYaw;
  accumulatedPitch = clampedPitch;
  targetYaw = clampedYaw;
  targetPitch = clampedPitch;
  
  showNotification(`TARGET: AZ ${clampedYaw.toFixed(1)}° EL ${clampedPitch.toFixed(1)}°`);
}

function clearTarget() {
  // TODO: Send clear target command when backend supports it
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
      handleWASDKey(e.code, true);
      break;
    case 'KeyR':
      if (currentMode === 'FREECAM') {
        // Stop all movement first
        stopHoldSlew();
        // Reset all key states
        keyState['KeyW'] = false;
        keyState['KeyA'] = false;
        keyState['KeyS'] = false;
        keyState['KeyD'] = false;
        keyTimers['KeyW'] = 0;
        keyTimers['KeyA'] = 0;
        keyTimers['KeyS'] = 0;
        keyTimers['KeyD'] = 0;
        // Reset accumulated position to center (smoothing will interpolate)
        accumulatedYaw = 0;
        accumulatedPitch = 0;
        targetYaw = 0;
        targetPitch = 0;
        showNotification('GIMBAL: CENTERED (0°, 0°)');
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
    handleWASDKey(e.code, false);
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
// Boot
// ---------------------------------------------------------------------------
connect();
