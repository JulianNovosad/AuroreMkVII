# Full System Integration Design

**Date:** 2026-04-01
**Status:** Auto-approved

## Goal

Wire the web interface to the C++ binary's state machine, add YOLO26n target detection (person/car/airplane), an autonomous oval sweep pattern for SEARCH mode, and a command bridge so the browser actually controls the turret.

---

## Architecture

### Command Flow (Browser → C++)

```
Browser WS JSON  →  Node.js /ws  →  CommandSocket (UNIX /tmp/aurore_cmd.sock)  →  StateMachine
```

Node.js translates browser JSON commands to a simple text protocol over a UNIX domain socket. C++ reads and dispatches to state machine methods. Telemetry already flows C++ → Node.js → browser via the existing HUD socket.

**Text protocol (newline-delimited):**
```
MODE AUTO\n       → request_search()
MODE FREECAM\n    → request_freecam()
MODE IDLE\n       → request_cancel()
FREECAM AZ EL\n   → gimbal_ctrl.command_absolute(az, el)
RESET\n           → on_manual_reset()
```

### Detection Architecture (non-blocking)

YOLO26n inference takes ~30-50ms on RPi5 CPU. This cannot run synchronously in the 8.333ms RT track_compute thread. A dedicated non-RT **detect thread** runs YOLO asynchronously.

```
vision_pipeline  →  frame_buffer  →  track_compute (RT, 120Hz)
                                              │
                              every 4th frame: copy resized 640×360 BGR
                                              │
                              detect_shared.frame_mtx (brief copy)
                                              │
                                       detect thread (non-RT)
                                       YOLO26n @ ~15-20fps
                                              │
                              detect_shared.result (try_lock, non-blocking in RT)
```

track_compute only does `try_lock` — never blocks.

### SEARCH State Flow

```
SEARCH:
  SweepPattern.tick(dt) → gimbal_ctrl.command_absolute(az, el)
  try_lock detect_result:
    if detection (class 0/2/4, conf > 0.4):
      stop sweep
      init KcfTracker on bbox
      state_machine.on_detection()
      → TRACKING
```

### TRACKING State Flow (unchanged)

KCF tracker runs every frame. On track loss → redetect → if failed → SEARCH, resume sweep.

### Sweep Pattern

Lissajous curve for smooth oval coverage:
```
az(t) = az_amplitude * sin(2π * t / T_az)
el(t) = el_offset + el_amplitude * sin(2π * t / T_el + π/2)
```
With `T_el = T_az / 2`, this traces an oval (figure-8 variant) covering the full FOV.
Default: `az_amplitude=80°`, `el_amplitude=15°`, `T_az=10s`, `el_offset=10°`.

---

## New Components

| Component | Files | Purpose |
|-----------|-------|---------|
| `Yolo26Detector` | `include/aurore/yolo26_detector.hpp`, `src/vision/yolo26_detector.cpp` | ONNX Runtime inference wrapper, letterbox preprocessing, class filter |
| `SweepPattern` | `include/aurore/sweep_pattern.hpp`, `src/vision/sweep_pattern.cpp` | Parametric oval gimbal scan |
| `CommandSocket` | `include/aurore/command_socket.hpp`, `src/common/command_socket.cpp` | UNIX socket command bridge |
| `scripts/export_yolo26n.py` | Python script | Export yolo26n.pt → models/yolo26n.onnx |

## Modified Components

| Component | Change |
|-----------|--------|
| `src/main.cpp` | Add detect thread, CommandSocket startup, SweepPattern in track_compute |
| `aurore-link/server.js` | Add CommandSocket client, forward browser WS commands |
| `CMakeLists.txt` | Add ONNX Runtime linkage, new source files |

---

## YOLO26n ONNX Export

```python
from ultralytics import YOLO
model = YOLO("yolo26n.pt")
model.export(format="onnx", imgsz=640)  # end-to-end NMS-free by default
```

Input: `[1, 3, 640, 640]` float32 normalized [0,1], letterboxed.
Output: `[1, 300, 6]` → `[x1, y1, x2, y2, confidence, class_id]` in 640×640 space.
Target classes: `{0: person, 2: car, 4: airplane}`.

---

## Out of Scope

- ARMED state / fire command (requires physical safety interlock integration)
- Custom-trained model (uses COCO pretrained weights)
- XNNPACK EP (not in Debian libonnxruntime 1.21 build; CPU EP has NEON via Eigen)
