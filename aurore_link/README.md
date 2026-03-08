# Aurore MkVII Python Client

Python client library and viewer for Aurore MkVII telemetry and command interface.

## Setup

```bash
pip3 install protobuf opencv-python
```

## Usage

### Telemetry Viewer (MVP)

```bash
cd aurore_link
python3 viewer.py 127.0.0.1
```

**Keyboard controls:**
- `a` - Switch to AUTO mode
- `f` - Switch to FREECAM mode
- `q` - Quit

### Programmatic Usage

```python
from client import AuroreClient

def on_telemetry(telemetry):
    print(f"Mode: {telemetry.health.mode}")
    print(f"FCS State: {telemetry.health.fcs_state}")
    print(f"Gimbal AZ: {telemetry.gimbal.az_deg:.2f}")
    print(f"Gimbal EL: {telemetry.gimbal.el_deg:.2f}")
    if telemetry.track.valid:
        print(f"Track: ({telemetry.track.centroid_x}, {telemetry.track.centroid_y})")

client = AuroreClient(host="127.0.0.1")
client.on_telemetry = on_telemetry

if client.connect():
    client.send_mode_switch("AUTO")
    client.send_freecam(45.0, 30.0, 10.0)
    # Keep running to receive telemetry
    import time
    while True:
        time.sleep(1)
```

## Architecture

- **Telemetry port (9000)**: Receives `Telemetry` messages from MkVII
- **Command port (9002)**: Sends `Command` messages to MkVII
- **Framing**: Length-prefixed protobuf (4-byte big-endian length + data)

## Files

- `aurore_pb2.py` - Generated protobuf bindings
- `client.py` - TCP client library
- `viewer.py` - OpenCV telemetry viewer
