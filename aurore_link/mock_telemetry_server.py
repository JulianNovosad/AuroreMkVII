#!/usr/bin/env python3
"""
Mock telemetry server for testing the Aurore Python client.

Simulates MkVII telemetry output on port 9000 and accepts commands on port 9002.
"""

import socket
import struct
import threading
import time
import random

import aurore_pb2
from aurore_pb2 import (
    Telemetry, TrackState, BallisticSolution, GimbalStatus, SystemHealth,
    ProtoFcsState, OperatingMode
)


def send_telemetry(sock: socket.socket, telemetry: Telemetry):
    """Send length-prefixed telemetry message."""
    data = telemetry.SerializeToString()
    length = struct.pack(">I", len(data))
    sock.sendall(length + data)


def build_telemetry(frame_count: int) -> Telemetry:
    """Build a sample telemetry message."""
    t = Telemetry()
    t.timestamp_ns = int(time.time() * 1e9)
    
    # Track state - simulate valid track with some noise
    t.track.valid = random.random() > 0.2
    if t.track.valid:
        t.track.centroid_x = 768.0 + random.uniform(-50, 50)
        t.track.centroid_y = 432.0 + random.uniform(-30, 30)
        t.track.velocity_x = random.uniform(-5, 5)
        t.track.velocity_y = random.uniform(-3, 3)
        t.track.confidence = random.uniform(0.7, 0.99)
        t.track.range_m = random.uniform(100, 500)
    
    # Ballistic solution
    t.ballistic.valid = t.track.valid
    if t.ballistic.valid:
        t.ballistic.az_lead_deg = random.uniform(-2, 2)
        t.ballistic.el_lead_deg = random.uniform(0.5, 3)
        t.ballistic.range_m = t.track.range_m
        t.ballistic.p_hit = random.uniform(0.6, 0.95)
    
    # Gimbal status
    t.gimbal.az_deg = random.uniform(-45, 45)
    t.gimbal.el_deg = random.uniform(10, 30)
    t.gimbal.az_error_deg = random.uniform(0, 0.5)
    t.gimbal.el_error_deg = random.uniform(0, 0.5)
    t.gimbal.settled = random.random() > 0.3
    
    # System health
    t.health.cpu_temp_c = random.uniform(45, 65)
    t.health.cpu_usage_pct = random.uniform(20, 60)
    t.health.frame_count = frame_count
    t.health.deadline_misses = 0
    t.health.emergency_active = False
    t.health.fcs_state = ProtoFcsState.PROTO_TRACKING
    t.health.mode = OperatingMode.AUTO
    
    return t


def telemetry_handler(client_socket: socket.socket, addr):
    """Handle telemetry client connection."""
    print(f"Telemetry client connected: {addr}")
    frame_count = 0
    try:
        while True:
            telemetry = build_telemetry(frame_count)
            send_telemetry(client_socket, telemetry)
            frame_count += 1
            time.sleep(0.008333)  # 120Hz
    except (socket.error, BrokenPipeError):
        print(f"Telemetry client disconnected: {addr}")
    finally:
        client_socket.close()


def command_handler(client_socket: socket.socket, addr):
    """Handle command client connection."""
    print(f"Command client connected: {addr}")
    try:
        while True:
            # Read length prefix
            length_bytes = client_socket.recv(4)
            if not length_bytes:
                break
            if len(length_bytes) < 4:
                continue
            
            length = struct.unpack(">I", length_bytes)[0]
            if length > 1024 * 1024:
                print(f"Command message too large: {length}")
                break
            
            # Read command data
            data = b""
            while len(data) < length:
                chunk = client_socket.recv(length - len(data))
                if not chunk:
                    break
                data += chunk
            
            if not data:
                break
            
            # Parse and log command
            try:
                cmd = aurore_pb2.Command()
                cmd.ParseFromString(data)
                payload = cmd.WhichOneof("payload")
                if payload == "mode_switch":
                    mode = "AUTO" if cmd.mode_switch.mode == OperatingMode.AUTO else "FREECAM"
                    print(f"Command: Mode switch to {mode}")
                elif payload == "freecam":
                    print(f"Command: Freecam AZ={cmd.freecam.az_deg:.1f} EL={cmd.freecam.el_deg:.1f} VEL={cmd.freecam.velocity_dps:.1f}")
                elif payload == "arm":
                    print(f"Command: Arm {'authorized' if cmd.arm.authorized else 'not authorized'}")
                elif payload == "config":
                    print(f"Command: Config patch with {len(cmd.config.values)} values")
            except Exception as e:
                print(f"Failed to parse command: {e}")
    except (socket.error, BrokenPipeError):
        print(f"Command client disconnected: {addr}")
    finally:
        client_socket.close()


def main():
    telemetry_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    telemetry_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    telemetry_server.bind(("0.0.0.0", 9000))
    telemetry_server.listen(5)
    print("Mock telemetry server listening on port 9000")
    
    command_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    command_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    command_server.bind(("0.0.0.0", 9002))
    command_server.listen(5)
    print("Mock command server listening on port 9002")
    
    def accept_telemetry():
        while True:
            client_socket, addr = telemetry_server.accept()
            threading.Thread(
                target=telemetry_handler,
                args=(client_socket, addr),
                daemon=True
            ).start()
    
    def accept_command():
        while True:
            client_socket, addr = command_server.accept()
            threading.Thread(
                target=command_handler,
                args=(client_socket, addr),
                daemon=True
            ).start()
    
    threading.Thread(target=accept_telemetry, daemon=True).start()
    threading.Thread(target=accept_command, daemon=True).start()
    
    print("Mock server running. Press Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        telemetry_server.close()
        command_server.close()


if __name__ == "__main__":
    main()
