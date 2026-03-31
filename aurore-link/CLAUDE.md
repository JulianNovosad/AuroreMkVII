# CLAUDE.md — aurore-link

Web-based remote control station and calibration interface for Aurore MkVII.

## Overview

Vanilla JS/CSS SPA with a Node.js server. No build step, no bundler, no framework.
Provides two interfaces:
- **HUD** (`/`) — AC-130 style tactical display with WebSocket telemetry
- **Calibration** (`/calibrate`) — 3-step servo/LRF/camera calibration workflow

## Runtime Requirements

- Node.js 18+
- `npm install` (installs `ws` package only)
- Optional hardware: libcamera-vid, ffmpeg, Fusion HAT+, UART LRF

## Running

```bash
cd /home/pi/AuroreMkVII/aurore-link
npm install
node server.js
# HUD:          http://localhost:8080/
# Calibration:  http://localhost:8080/calibrate
```

## File Map

| File | Purpose |
|------|---------|
| `server.js` | Node.js HTTP server + WebSocket routing (HUD + calibration) |
| `index.html` | HUD SPA shell: canvas, SVG reticle, tactical overlays |
| `style.css` | HUD styles: AC-130 military aesthetic, scanlines |
| `main.js` | HUD logic: WS client, canvas animation, gimbal smoothing |
| `calibrate.html` | Calibration SPA: dual camera feeds, 3-step workflow |
| `calibrate.css` | Calibration styles: jog controls, step tabs |
| `calibrate.js` | Calibration logic: servo jog, LRF parser, save workflow |

## Endpoints

### HTTP
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | HUD interface |
| `/calibrate` | GET | Calibration interface |
| `/stream/mipi` | GET | MJPEG stream from libcamera-vid (1536×864@60) |
| `/stream/usb` | GET | MJPEG stream from ffmpeg (1280×720@60) |
| `/api/servo/center` | POST | Center both servos to 90° |
| `/api/servo/angle` | POST | Set servo angles `{pan_deg, tilt_deg}` |
| `/api/calibration/save` | POST | Save calibration to `config/calibration.json` |

### WebSocket
| Endpoint | Description |
|----------|-------------|
| `/ws` | HUD telemetry (150ms interval) |
| `/ws/calib` | Calibration: servo state + LRF distance (~10Hz) |

## WebSocket Protocol

**Server → Client** (every 150ms):
```json
{
  "ts": 1710000000000,
  "mode": "AUTO",
  "fcs_state": "TRACKING",
  "frame_count": 12847,
  "gimbal": {"yaw": 12.4, "pitch": -3.2},
  "track": {"valid": true, "cx": 768, "cy": 432, "w": 120, "h": 80,
            "confidence": 0.87, "range_m": 245.3, "vx": 2.1, "vy": -0.3},
  "ballistic": {"az_lead_mrad": 1.2, "el_lead_mrad": -0.8, "p_hit": 0.72},
  "health": {"cpu_temp": 67.3, "cpu_pct": 34.2, "deadline_misses": 0}
}
```

**Client → Server** (on user action):
```json
{"type": "mode_switch", "mode": "FREECAM"}
{"type": "freecam", "az": 12.4, "el": -3.2}
```

## Style Conventions

- ES2022+, `const` by default
- No semicolons not enforced — project uses them
- CSS variables in `:root` for all colors
- No external CSS libraries

## Layout

- **Laptop** (default): CSS grid `"video sidebar"`, video fills remaining space
- **Phone** (≤640px or `.phone-view`): single column stack
