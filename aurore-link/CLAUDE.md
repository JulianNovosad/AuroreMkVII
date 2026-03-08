# CLAUDE.md — aurore-link

Web-based remote control station for Aurore MkVII (pre-hardware validation MVP).

## Overview

Vanilla JS/CSS SPA with a Node.js mock server. No build step, no bundler, no framework.
Uses WebSocket + JSON to communicate with the mock server (not the raw TCP C++ server).

## Runtime Requirements

- Node.js 18+
- `npm install` (installs `ws` package only)

## Running

```bash
cd /home/laptop/AuroreMkVII/aurore-link
npm install
node mock-server.js
# Open http://localhost:8080 in browser
```

## File Map

| File | Purpose |
|------|---------|
| `mock-server.js` | Node.js HTTP static server + WebSocket on port 8080 |
| `index.html` | SPA shell: canvas, SVG HUD, sidebar/strip |
| `style.css` | Responsive layout, CSS variables, HUD styles |
| `main.js` | WS client, canvas animation, SVG HUD renderer, controls |

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
