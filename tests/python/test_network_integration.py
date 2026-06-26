"""
Network Integration Tests

Connects to a running aurore binary via TCP and validates the
telemetry and command protocol. Requires the aurore process to be
running on the target system.

References:
- AuroreClient in aurore_link/client.py
- ICD-004 Telemetry format in proto/aurore.proto
- Ports: telemetry=9000, command=9002
"""
import socket
import struct
import threading
import time

import pytest

from aurore_pb2 import Telemetry, Command, OperatingMode, ProtoFcsState


pytestmark = [
    pytest.mark.skipif(
        True,
        reason="requires running aurore binary on localhost:9000/9002",
    ),
    pytest.mark.network,
]


@pytest.fixture(scope="module")
def telemetry_socket(telemetry_port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect(("127.0.0.1", telemetry_port))
        yield sock
    finally:
        sock.close()


@pytest.fixture(scope="module")
def command_socket(command_port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect(("127.0.0.1", command_port))
        yield sock
    finally:
        sock.close()


def recv_exact(sock: socket.socket, n: int) -> bytes:
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return data


class TestTelemetryStream:
    def test_receive_telemetry_frame(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        assert 0 < length < (10 * 1024 * 1024)
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        assert t.timestamp_ns > 0

    def test_telemetry_has_required_fields(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        assert t.HasField("track")
        assert t.HasField("ballistic")
        assert t.HasField("gimbal")
        assert t.HasField("health")

    def test_telemetry_fcs_state_valid(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        assert ProtoFcsState.Name(t.health.fcs_state) != ""

    def test_multiple_frames_have_monotonic_timestamps(
        self, telemetry_socket: socket.socket
    ):
        prev_ts = 0
        for _ in range(5):
            length_bytes = recv_exact(telemetry_socket, 4)
            length = struct.unpack(">I", length_bytes)[0]
            payload = recv_exact(telemetry_socket, length)
            t = Telemetry()
            t.ParseFromString(payload)
            assert t.timestamp_ns >= prev_ts, (
                f"non-monotonic timestamp: {t.timestamp_ns} < {prev_ts}"
            )
            prev_ts = t.timestamp_ns

    def test_track_state_valid_when_active(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        if t.track.valid:
            assert 0.0 <= t.track.centroid_x <= 1536.0
            assert 0.0 <= t.track.centroid_y <= 864.0
            assert 0.0 <= t.track.confidence <= 1.0

    def test_gimbal_angles_in_range(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        assert -180.0 <= t.gimbal.az_deg <= 180.0
        assert -90.0 <= t.gimbal.el_deg <= 90.0

    def test_ballistic_p_hit_in_range(self, telemetry_socket: socket.socket):
        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        if t.ballistic.valid:
            assert 0.0 <= t.ballistic.p_hit <= 1.0

    def test_telemetry_rate(self, telemetry_socket: socket.socket):
        frames = []
        start = time.monotonic()
        timeout = start + 1.0
        while time.monotonic() < timeout:
            length_bytes = recv_exact(telemetry_socket, 4)
            length = struct.unpack(">I", length_bytes)[0]
            payload = recv_exact(telemetry_socket, length)
            t = Telemetry()
            t.ParseFromString(payload)
            frames.append(t)
        elapsed = time.monotonic() - start
        fps = len(frames) / elapsed
        assert 50 <= fps <= 130, f"Telemetry rate {fps:.1f} fps outside expected range (50-130)"


class TestCommandInterface:
    def test_send_mode_switch_auto(
        self, command_socket: socket.socket, telemetry_socket: socket.socket
    ):
        c = Command()
        c.mode_switch.mode = OperatingMode.AUTO
        data = c.SerializeToString()
        framed = struct.pack(">I", len(data)) + data
        command_socket.sendall(framed)

        length_bytes = recv_exact(telemetry_socket, 4)
        length = struct.unpack(">I", length_bytes)[0]
        payload = recv_exact(telemetry_socket, length)
        t = Telemetry()
        t.ParseFromString(payload)
        assert t.health.mode == OperatingMode.AUTO

    def test_send_mode_switch_freecam(
        self, command_socket: socket.socket, telemetry_socket: socket.socket
    ):
        c = Command()
        c.mode_switch.mode = OperatingMode.FREECAM_MODE
        data = c.SerializeToString()
        framed = struct.pack(">I", len(data)) + data
        command_socket.sendall(framed)

    def test_send_freecam_command(self, command_socket: socket.socket):
        c = Command()
        c.freecam.az_deg = 0.0
        c.freecam.el_deg = 0.0
        c.freecam.velocity_dps = 10.0
        data = c.SerializeToString()
        framed = struct.pack(">I", len(data)) + data
        command_socket.sendall(framed)

    def test_send_arm_command(self, command_socket: socket.socket):
        c = Command()
        c.arm.authorized = False
        data = c.SerializeToString()
        framed = struct.pack(">I", len(data)) + data
        command_socket.sendall(framed)
