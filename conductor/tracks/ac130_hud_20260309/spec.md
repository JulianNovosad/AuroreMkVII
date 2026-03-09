# AC-130 HUD Frontend Redesign - Specification

## Overview

Redesign the aurore-link frontend to match real AC-130 gunship HUD aesthetics while preserving 100% of existing WebSocket functionality.

## Current State (Before Redesign)

### Tech Stack
- Vanilla HTML5 + CSS3 + JavaScript (ES2022+)
- Canvas API for video/thermal rendering
- SVG for HUD overlay elements
- WebSocket for real-time telemetry

### Current Layout
- Two-column grid (video area + 280px sidebar)
- Color-coded UI elements (green/amber/red/blue)
- Small centered crosshair
- Solid track box around targets
- Progress bar for P_hit
- Visible joystick control

### Current Data Structure
```javascript
{
  ts: number,           // Timestamp
  fcs_state: string,    // BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED, FAULT
  mode: string,         // AUTO, FREECAM
  gimbal: { yaw: number, pitch: number },
  track: { valid: boolean, cx: number, cy: number, w: number, h: number, confidence: number, range_m: number },
  ballistic: { az_lead_mrad: number, el_lead_mrad: number, p_hit: number },
  health: { cpu_temp: number, cpu_pct: number, deadline_misses: number }
}
```

## Target State (After Redesign)

### Visual Aesthetic
- **Monochrome palette:** White (#FFFFFF) on black, no color coding
- **Full-screen overlay:** Telemetry pushed to extreme edges
- **Large central reticle:** Full-screen stadia with mil-hash marks
- **Corner brackets:** L-shaped brackets around tracked targets
- **Offset pipper:** Secondary crosshair for ballistic lead
- **Analog dial:** Gimbal yaw indicator (lower-left)
- **Typography:** Share Tech Mono, aliased, uppercase only

### Layout Structure
```
┌─────────────────────────────────────────────────────────────────┐
│  SYS: ARMED          HH:MM:SS              WHT 67C  GAIN 34     │
│  PHIT 92                                                        │
│                                                                  │
│                    │           │                                │
│  [ gimbal dial ]   │    ╳      │   RNG 245M                     │
│                    │           │   TRK 85                       │
│                                                                  │
│  LNK: UP           AZ 123.4° EL 56.7°        WHOT BRT -- CNT -- │
│  [ ] AUTO                                                         │
└─────────────────────────────────────────────────────────────────┘
```

### Component Specifications

#### Main Reticle
- Full-screen stadia lines (horizontal + vertical)
- Central gap: 600px (lines stop 300px from center)
- Mil-hash marks every 100px (major) and 50px (fine)
- Stroke width: 1.5px
- Center dot: 2.5px radius

#### Corner Brackets
- L-shaped corners (two perpendicular lines)
- Arm length: 18px
- Stroke width: 1.5px
- Position: Tight to target bounding box

#### Ballistic Pipper
- 40×40 offset crosshair
- Stroke width: 1px
- No center dot
- Positioned based on az/el lead mrad

#### Analog Dial
- 150px diameter
- Cardinal labels: 0/90/180/270
- Tick marks every 5°
- Rotating needle bound to gimbal.yaw

#### Typography
- Font: Share Tech Mono (Google Fonts)
- Sizes: 14-32px based on hierarchy
- Text transform: uppercase
- No text-shadow or glow effects

## Functional Requirements

### Preserved Functionality
- [ ] WebSocket connection to `ws://localhost:8080/ws`
- [ ] JSON telemetry parsing (unchanged schema)
- [ ] Mode switch commands (`mode_switch`)
- [ ] FREECAM joystick commands (`freecam`)
- [ ] Auto-reconnect with exponential backoff
- [ ] Canvas thermal visualization (mock video)

### New Features
- [ ] FAULT state blink (3s cycle, opacity 100% ↔ 30%)
- [ ] Corner bracket positioning around targets
- [ ] Ballistic pipper positioning
- [ ] Analog dial rotation
- [ ] Horizontal scale bar with mil-ticks

### Removed Features
- [ ] Color-coded state badges (replaced with text)
- [ ] P_hit progress bar (replaced with text)
- [ ] Visible joystick (now invisible but functional)
- [ ] Sidebar panel (replaced with edge telemetry)
- [ ] CSS transitions (instant updates only)

## Performance Requirements

### Render Performance
- 60 FPS canvas animation
- Instant HUD updates (no CSS transitions)
- Zero layout thrashing

### Browser Compatibility
- Chrome/Chromium ≥100
- Firefox ≥100
- Safari ≥15

## Accessibility

- High contrast (white on black)
- Readable at glance (large fonts)
- No color-dependent information

## Security

- No authentication (local development only)
- WebSocket from same origin
- No sensitive data in telemetry

## Testing

### Manual Testing
- [ ] WebSocket connects and receives data
- [ ] All telemetry displays correctly
- [ ] Reticle renders with proper gap
- [ ] Corner brackets appear when tracking
- [ ] Pipper moves with ballistic data
- [ ] Analog dial rotates with yaw
- [ ] FAULT blinks at 3s cycle
- [ ] Joystick controls gimbal (invisible)
- [ ] Mode switch works

### Browser Testing
- [ ] Chrome on Linux
- [ ] Firefox on Linux
- [ ] Hard refresh loads new assets

## Success Criteria

1. **Visual Match:** HUD matches AC-130 reference aesthetic
2. **Functional Parity:** All existing features work
3. **Performance:** 60 FPS, no stuttering
4. **Code Quality:** Passes eslint, clean console

## Out of Scope

- Backend changes (WebSocket server unchanged)
- New telemetry fields
- Authentication/authorization
- Mobile responsive design (desktop focus)
