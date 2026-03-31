/**
 * Aurore MkVII — Calibration Interface
 * WebSocket client, servo jog controls, and 3-step calibration workflow
 */

'use strict';

// ===========================================================================
// State
// ===========================================================================

const state = {
  // Servo positions (degrees)
  pan: 90.0,
  tilt: 90.0,
  
  // LRF state
  lrfActive: false,
  lrfDistanceMm: null,
  
  // Calibration steps
  step1Done: false,
  step2Done: false,
  step3Done: false,
  
  // Step 2: LRF alignment data
  lrfAlignment: {
    pan_deg: null,
    tilt_deg: null,
    distance_mm: null,
  },
  
  // Step 3: Camera-to-camera offset
  cameraPoints: [],  // Array of {mipi: {x,y}, usb: {x,y}}
  
  // WebSocket
  ws: null,
  wsReconnectDelay: 1000,
};

// ===========================================================================
// DOM References
// ===========================================================================

// Status
const calStatus = document.getElementById('cal-status');

// Feed elements
const feedMipi = document.getElementById('feed-mipi');
const feedUsb = document.getElementById('feed-usb');
const lrfBadgeMipi = document.getElementById('lrf-badge-mipi');
const lrfBadgeUsb = document.getElementById('lrf-badge-usb');
const xhairMipi = document.getElementById('xhair-mipi');
const xhairUsb = document.getElementById('xhair-usb');

// Step tabs
const stepTabs = document.querySelectorAll('.step-tab');
const stepPanels = document.querySelectorAll('.step-panel');

// Step 1 elements
const btnCenterServos = document.getElementById('btn-center-servos');
const btnStep1Done = document.getElementById('btn-step1-done');
const panReadout = document.getElementById('pan-readout');
const tiltReadout = document.getElementById('tilt-readout');

// Step 2 elements
const btnLrfToggle = document.getElementById('btn-lrf-toggle');
const lrfStatus = document.getElementById('lrf-status');
const lrfDistance = document.getElementById('lrf-distance');
const btnMarkAligned = document.getElementById('btn-mark-aligned');
const btnSaveCenterOffset = document.getElementById('btn-save-center-offset');
const panReadoutS2 = document.getElementById('pan-readout-s2');
const tiltReadoutS2 = document.getElementById('tilt-readout-s2');

// Step 3 elements
const btnClearPoints = document.getElementById('btn-clear-points');
const pointsCount = document.getElementById('points-count');
const pixelOffset = document.getElementById('pixel-offset');
const btnSaveCameraOffset = document.getElementById('btn-save-camera-offset');

// Footer
const btnSaveAll = document.getElementById('btn-save-all');
const saveStatus = document.getElementById('save-status');

// ===========================================================================
// WebSocket Client
// ===========================================================================

function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const host = window.location.host || 'localhost:8080';
  const wsUrl = `${protocol}//${host}/ws/calib`;
  
  console.log('[WS] Connecting to:', wsUrl);
  state.ws = new WebSocket(wsUrl);
  
  state.ws.addEventListener('open', () => {
    console.log('[WS] Connected');
    calStatus.textContent = 'WS: CONNECTED';
    calStatus.style.color = '#00ff00';
    state.wsReconnectDelay = 1000;
  });
  
  state.ws.addEventListener('message', (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      console.log('[WS] Message:', msg);
      handleWsMessage(msg);
    } catch (err) {
      console.warn('[WS] Bad JSON:', err);
    }
  });
  
  state.ws.addEventListener('close', () => {
    console.log('[WS] Disconnected');
    calStatus.textContent = 'WS: DISCONNECTED';
    calStatus.style.color = '#ff4444';
    setTimeout(connectWebSocket, state.wsReconnectDelay);
    state.wsReconnectDelay = Math.min(state.wsReconnectDelay * 1.5, 10000);
  });
  
  state.ws.addEventListener('error', (err) => {
    console.error('[WS] Error:', err);
    calStatus.textContent = 'WS: ERROR';
    calStatus.style.color = '#ff4444';
  });
}

function handleWsMessage(msg) {
  // Update servo state
  if (typeof msg.pan_deg === 'number') {
    state.pan = msg.pan_deg;
    updateServoReadouts();
  }
  if (typeof msg.tilt_deg === 'number') {
    state.tilt = msg.tilt_deg;
    updateServoReadouts();
  }

  // Update LRF state
  if (typeof msg.lrf_mm === 'number') {
    state.lrfDistanceMm = msg.lrf_mm;
    updateLrfDisplay();
    // Enable Mark Aligned button when we have LRF reading
    if (btnMarkAligned && btnMarkAligned.disabled) {
      btnMarkAligned.disabled = false;
    }
  }

  // Error handling
  if (msg.error === 'lrf_unavailable') {
    console.error('[LRF] Unavailable:', msg.detail);
    lrfStatus.textContent = 'LRF: ERROR';
    lrfStatus.classList.add('error');
  }
}

function sendWsMessage(msg) {
  if (state.ws && state.ws.readyState === WebSocket.OPEN) {
    state.ws.send(JSON.stringify(msg));
  }
}

// ===========================================================================
// Servo Readout Updates
// ===========================================================================

function updateServoReadouts() {
  panReadout.textContent = `PAN: ${state.pan.toFixed(1)}°`;
  tiltReadout.textContent = `TILT: ${state.tilt.toFixed(1)}°`;
  panReadoutS2.textContent = `PAN: ${state.pan.toFixed(1)}°`;
  tiltReadoutS2.textContent = `TILT: ${state.tilt.toFixed(1)}°`;
}

// ===========================================================================
// LRF Display Updates
// ===========================================================================

function updateLrfDisplay() {
  if (state.lrfActive && state.lrfDistanceMm !== null) {
    lrfBadgeMipi.textContent = `LRF: ${state.lrfDistanceMm}mm`;
    lrfBadgeUsb.textContent = `LRF: ${state.lrfDistanceMm}mm`;
    lrfBadgeMipi.classList.add('active');
    lrfBadgeUsb.classList.add('active');
    lrfDistance.textContent = `${state.lrfDistanceMm} mm`;
  } else {
    lrfBadgeMipi.textContent = 'LRF: --';
    lrfBadgeUsb.textContent = 'LRF: --';
    lrfBadgeMipi.classList.remove('active');
    lrfBadgeUsb.classList.remove('active');
    lrfDistance.textContent = '-- mm';
  }
}

// ===========================================================================
// Step Tab Navigation
// ===========================================================================

function switchToStep(stepNum) {
  // Update tabs
  stepTabs.forEach((tab) => {
    const tabStep = parseInt(tab.dataset.step, 10);
    tab.classList.toggle('active', tabStep === stepNum);
    tab.disabled = tabStep > getCurrentUnlockedStep();
  });
  
  // Update panels
  stepPanels.forEach((panel) => {
    panel.classList.toggle('active', panel.id === `step-${stepNum}`);
  });
}

function getCurrentUnlockedStep() {
  if (state.step3Done) return 3;
  if (state.step2Done) return 2;
  if (state.step1Done) return 2;
  return 1;
}

// ===========================================================================
// Step 1: Servo Centering
// ===========================================================================

btnCenterServos.addEventListener('click', async () => {
  console.log('[UI] Center Servos button clicked');
  try {
    console.log('[Servo] Fetching /api/servo/center');
    const res = await fetch('/api/servo/center', { method: 'POST' });
    console.log('[Servo] Response status:', res.status);
    const data = await res.json();
    console.log('[Servo] Response data:', data);
    if (data.ok) {
      state.pan = data.pan_deg;
      state.tilt = data.tilt_deg;
      updateServoReadouts();
      // Enable the "Done" button after centering
      btnStep1Done.disabled = false;
      showNotification('Servos centered to 90°/90° — click "Done" to continue', 'success');
    } else {
      showNotification('Failed to center servos: ' + data.error, 'error');
    }
  } catch (err) {
    console.error('[Servo] Network error:', err);
    showNotification('Network error: ' + err.message, 'error');
  }
});

btnStep1Done.addEventListener('click', () => {
  state.step1Done = true;
  btnStep1Done.disabled = true;
  switchToStep(2);
  showNotification('Step 1 complete — proceed to LRF alignment', 'success');
  updateSaveAllButton();
});

// Jog controls for Step 1
document.querySelectorAll('#step-1 .btn-jog').forEach((btn) => {
  btn.addEventListener('click', async () => {
    const axis = btn.dataset.axis;
    const delta = parseInt(btn.dataset.delta, 10);
    console.log('[UI] Jog button clicked:', axis, delta);
    
    if (axis === 'pan') {
      state.pan = Math.max(0, Math.min(180, state.pan + delta));
    } else if (axis === 'tilt') {
      state.tilt = Math.max(0, Math.min(180, state.tilt + delta));
    }
    
    console.log('[Servo] New position:', { pan: state.pan, tilt: state.tilt });
    await sendServoAngle(state.pan, state.tilt);
    updateServoReadouts();
    // Enable the "Done" button after any manual adjustment
    btnStep1Done.disabled = false;
  });
});

// ===========================================================================
// Step 2: LRF Alignment
// ===========================================================================

btnLrfToggle.addEventListener('click', () => {
  state.lrfActive = !state.lrfActive;
  
  if (state.lrfActive) {
    btnLrfToggle.classList.add('active');
    btnLrfToggle.textContent = 'Deactivate LRF Laser';
    lrfStatus.textContent = 'LRF: ON';
    lrfStatus.classList.add('active');
    sendWsMessage({ type: 'lrf_start' });
  } else {
    btnLrfToggle.classList.remove('active');
    btnLrfToggle.textContent = 'Activate LRF Laser';
    lrfStatus.textContent = 'LRF: OFF';
    lrfStatus.classList.remove('active');
    state.lrfDistanceMm = null;
    updateLrfDisplay();
    sendWsMessage({ type: 'lrf_stop' });
  }
});

btnMarkAligned.addEventListener('click', () => {
  if (state.lrfDistanceMm === null) {
    showNotification('No LRF distance reading available', 'error');
    return;
  }
  
  state.lrfAlignment.pan_deg = state.pan;
  state.lrfAlignment.tilt_deg = state.tilt;
  state.lrfAlignment.distance_mm = state.lrfDistanceMm;
  
  // Turn crosshairs green
  xhairMipi.classList.add('aligned');
  xhairUsb.classList.add('aligned');
  
  btnSaveCenterOffset.disabled = false;
  showNotification('Alignment marked — click "Save Center Offset" to store', 'success');
});

btnSaveCenterOffset.addEventListener('click', () => {
  state.step2Done = true;
  switchToStep(3);
  showNotification('Center offset saved — proceed to camera offset calibration', 'success');
  updateSaveAllButton();
});

// Jog controls for Step 2
document.querySelectorAll('#step-2 .btn-jog').forEach((btn) => {
  btn.addEventListener('click', async () => {
    const axis = btn.dataset.axis;
    const delta = parseInt(btn.dataset.delta, 10);
    
    if (axis === 'pan') {
      state.pan = Math.max(0, Math.min(180, state.pan + delta));
    } else if (axis === 'tilt') {
      state.tilt = Math.max(0, Math.min(180, state.tilt + delta));
    }
    
    await sendServoAngle(state.pan, state.tilt);
    updateServoReadouts();
  });
});

// ===========================================================================
// Step 3: Camera-to-Camera Offset
// ===========================================================================

let clickTarget = null;  // 'mipi' or 'usb'

feedMipi.addEventListener('click', (ev) => {
  if (clickTarget === 'usb') return;  // Wait for USB click first
  
  const rect = feedMipi.getBoundingClientRect();
  const x = ((ev.clientX - rect.left) / rect.width) * 100;
  const y = ((ev.clientY - rect.top) / rect.height) * 100;
  
  clickTarget = 'mipi';
  state.currentMipiPoint = { x, y };
  
  showNotification('Now click corresponding point on USB feed', 'info');
});

feedUsb.addEventListener('click', (ev) => {
  if (clickTarget !== 'mipi') return;  // Must click MIPI first
  
  const rect = feedUsb.getBoundingClientRect();
  const x = ((ev.clientX - rect.left) / rect.width) * 100;
  const y = ((ev.clientY - rect.top) / rect.height) * 100;
  
  state.cameraPoints.push({
    mipi: state.currentMipiPoint,
    usb: { x, y },
  });
  
  clickTarget = null;
  state.currentMipiPoint = null;
  
  updateCameraOffsetDisplay();
  showNotification(`Point pair recorded (${state.cameraPoints.length} total)`, 'success');
});

btnClearPoints.addEventListener('click', () => {
  state.cameraPoints = [];
  updateCameraOffsetDisplay();
  showNotification('All points cleared', 'info');
});

function updateCameraOffsetDisplay() {
  pointsCount.textContent = `Points: ${state.cameraPoints.length}`;
  
  if (state.cameraPoints.length === 0) {
    pixelOffset.textContent = 'Offset: (0, 0)';
    btnSaveCameraOffset.disabled = true;
    return;
  }
  
  // Calculate average offset
  let sumDx = 0;
  let sumDy = 0;
  
  state.cameraPoints.forEach((pair) => {
    sumDx += pair.usb.x - pair.mipi.x;
    sumDy += pair.usb.y - pair.mipi.y;
  });
  
  const avgDx = sumDx / state.cameraPoints.length;
  const avgDy = sumDy / state.cameraPoints.length;
  
  pixelOffset.textContent = `Offset: (${avgDx.toFixed(1)}, ${avgDy.toFixed(1)})`;
  
  if (state.cameraPoints.length >= 1) {
    btnSaveCameraOffset.disabled = false;
  }
}

btnSaveCameraOffset.addEventListener('click', () => {
  state.step3Done = true;
  showNotification('Camera offset saved', 'success');
  updateSaveAllButton();
});

// ===========================================================================
// Servo API Helper
// ===========================================================================

async function sendServoAngle(pan_deg, tilt_deg) {
  try {
    console.log('[Servo] Sending:', { pan_deg, tilt_deg });
    const res = await fetch('/api/servo/angle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pan_deg, tilt_deg }),
    });
    const data = await res.json();
    console.log('[Servo] Response:', data);
    if (!data.ok) {
      console.error('[Servo] Error:', data.error);
      showNotification('Servo error: ' + data.error, 'error');
    }
  } catch (err) {
    console.error('[Servo] Network error:', err);
    showNotification('Servo network error: ' + err.message, 'error');
  }
}

// ===========================================================================
// Save All Calibration Data
// ===========================================================================

function updateSaveAllButton() {
  btnSaveAll.disabled = !(state.step1Done && state.step2Done);
}

btnSaveAll.addEventListener('click', async () => {
  const calData = {
    servo: {
      pan_center_deg: state.pan,
      tilt_center_deg: state.tilt,
    },
    lrf: { ...state.lrfAlignment },
  };
  
  if (state.step3Done && state.cameraPoints.length > 0) {
    // Calculate average pixel offset
    let sumDx = 0;
    let sumDy = 0;
    
    state.cameraPoints.forEach((pair) => {
      sumDx += pair.usb.x - pair.mipi.x;
      sumDy += pair.usb.y - pair.mipi.y;
    });
    
    calData.cameras = {
      pixel_offset_x: sumDx / state.cameraPoints.length,
      pixel_offset_y: sumDy / state.cameraPoints.length,
      sample_count: state.cameraPoints.length,
    };
  }
  
  try {
    const res = await fetch('/api/calibration/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(calData),
    });
    const data = await res.json();
    
    if (data.ok) {
      saveStatus.textContent = `Saved to ${data.file} at ${data.saved_at}`;
      saveStatus.classList.add('success');
      saveStatus.classList.remove('error');
      showNotification('Calibration data saved successfully', 'success');
    } else {
      throw new Error(data.error);
    }
  } catch (err) {
    saveStatus.textContent = `Error: ${err.message}`;
    saveStatus.classList.add('error');
    saveStatus.classList.remove('success');
    showNotification('Failed to save calibration: ' + err.message, 'error');
  }
});

// ===========================================================================
// Notification System
// ===========================================================================

let notificationTimeout = null;

function showNotification(message, type = 'info') {
  // Remove existing notification
  const existing = document.getElementById('cal-notification');
  if (existing) {
    existing.remove();
  }
  
  const notif = document.createElement('div');
  notif.id = 'cal-notification';
  notif.style.cssText = `
    position: fixed;
    bottom: 100px;
    left: 50%;
    transform: translateX(-50%);
    background: rgba(0, 0, 0, 0.9);
    border: 1px solid ${type === 'error' ? '#ff4444' : type === 'success' ? '#00ff00' : '#ffffff'};
    color: #ffffff;
    padding: 12px 24px;
    font-family: 'Share Tech Mono', monospace;
    font-size: 16px;
    z-index: 1000;
    pointer-events: none;
    opacity: 1;
    transition: opacity 0.5s ease-out;
  `;
  
  notif.textContent = message;
  document.body.appendChild(notif);
  
  if (notificationTimeout) {
    clearTimeout(notificationTimeout);
  }
  
  notificationTimeout = setTimeout(() => {
    notif.style.opacity = '0';
    setTimeout(() => notif.remove(), 500);
  }, 4000);
}

// ===========================================================================
// Initialize
// ===========================================================================

function init() {
  connectWebSocket();
  updateServoReadouts();
  updateLrfDisplay();
  updateSaveAllButton();
  
  // Start on step 1
  switchToStep(1);
  
  console.log('[CAL] Calibration interface initialized');
}

init();
