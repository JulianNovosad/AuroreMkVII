"""
Client Protocol Tests

Tests the length-prefixed protobuf framing used by AuroreClient
for TCP communication with the aurore binary on ports 9000 (telemetry)
and 9002 (command).

Protocol: 4-byte big-endian length prefix + serialized protobuf message.
"""
import struct

import pytest

from aurore_pb2 import Telemetry, Command, OperatingMode


LENGTH_PREFIX_FORMAT = ">I"
LENGTH_PREFIX_SIZE = struct.calcsize(LENGTH_PREFIX_FORMAT)  # 4 bytes


def length_prefix(data: bytes) -> bytes:
    return struct.pack(LENGTH_PREFIX_FORMAT, len(data))


def frame_message(data: bytes) -> bytes:
    return length_prefix(data) + data


def parse_frame(buffer: bytes) -> tuple:
    if len(buffer) < LENGTH_PREFIX_SIZE:
        raise ValueError("buffer too small for length prefix")
    length = struct.unpack(LENGTH_PREFIX_FORMAT, buffer[:LENGTH_PREFIX_SIZE])[0]
    payload_start = LENGTH_PREFIX_SIZE
    payload_end = payload_start + length
    if len(buffer) < payload_end:
        raise ValueError("buffer too small for full frame")
    return length, buffer[payload_start:payload_end]


class TestLengthPrefix:
    def test_prefix_size(self):
        assert LENGTH_PREFIX_SIZE == 4

    def test_prefix_matches_data_length(self):
        data = b"\x00" * 100
        prefixed = frame_message(data)
        length, payload = parse_frame(prefixed)
        assert length == 100
        assert payload == data

    def test_empty_payload(self):
        data = b""
        prefixed = frame_message(data)
        length, payload = parse_frame(prefixed)
        assert length == 0
        assert payload == b""

    def test_large_payload(self):
        data = b"\x01" * 65535
        prefixed = frame_message(data)
        length, payload = parse_frame(prefixed)
        assert length == 65535
        assert payload == data


class TestTelemetryFraming:
    def test_frame_telemetry_message(self):
        t = Telemetry()
        t.timestamp_ns = 1000
        t.health.fcs_state = 4
        data = t.SerializeToString()
        framed = frame_message(data)
        length, payload = parse_frame(framed)
        assert length == len(data)
        t2 = Telemetry()
        t2.ParseFromString(payload)
        assert t2.timestamp_ns == 1000

    def test_telemetry_frame_boundary_integrity(self):
        t = Telemetry()
        t.timestamp_ns = 42
        data = t.SerializeToString()
        framed = frame_message(data)
        length = struct.unpack(LENGTH_PREFIX_FORMAT, framed[:4])[0]
        assert length == len(data)
        assert len(framed) == 4 + len(data)

    def test_multiple_telemetry_frames(self):
        frames = []
        for i in range(10):
            t = Telemetry()
            t.timestamp_ns = i
            frames.append(frame_message(t.SerializeToString()))
        buffer = b"".join(frames)
        offset = 0
        for i in range(10):
            chunk = buffer[offset:]
            length, payload = parse_frame(chunk)
            t2 = Telemetry()
            t2.ParseFromString(payload)
            assert t2.timestamp_ns == i
            offset += 4 + length


class TestCommandFraming:
    def test_frame_command_message(self):
        c = Command()
        c.mode_switch.mode = OperatingMode.AUTO
        data = c.SerializeToString()
        framed = frame_message(data)
        length, payload = parse_frame(framed)
        assert length == len(data)
        c2 = Command()
        c2.ParseFromString(payload)
        assert c2.mode_switch.mode == OperatingMode.AUTO

    def test_command_freecam_framing(self):
        c = Command()
        c.freecam.az_deg = 45.0
        c.freecam.el_deg = 30.0
        c.freecam.velocity_dps = 10.0
        data = c.SerializeToString()
        framed = frame_message(data)
        _, payload = parse_frame(framed)
        c2 = Command()
        c2.ParseFromString(payload)
        assert c2.WhichOneof("payload") == "freecam"
        assert abs(c2.freecam.az_deg - 45.0) < 0.01


class TestProtocolEdgeCases:
    def test_zero_length_prefix(self):
        buffer = b"\x00\x00\x00\x00"
        length, payload = parse_frame(buffer)
        assert length == 0
        assert payload == b""

    def test_rejects_truncated_frame(self):
        data = b"\x00\x00\x00\x05" + b"\x01\x02"
        with pytest.raises(ValueError):
            parse_frame(data)

    def test_rejects_empty_buffer(self):
        with pytest.raises(ValueError):
            parse_frame(b"")

    def test_rejects_short_buffer(self):
        with pytest.raises(ValueError):
            parse_frame(b"\x00\x00")

    def test_max_reasonable_message_size(self):
        max_size = 10 * 1024 * 1024
        length_bytes = struct.pack(LENGTH_PREFIX_FORMAT, max_size)
        assert len(length_bytes) == 4
        assert struct.unpack(LENGTH_PREFIX_FORMAT, length_bytes)[0] == max_size
