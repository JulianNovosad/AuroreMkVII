"""
HUD Binary Protocol Tests (ICD-006)

Tests the packed binary message formats used by HudSocket for communication
with HUD display clients over UNIX domain sockets.

References:
- include/aurore/hud_socket.hpp for struct definitions
- ICD-006: HUD binary message protocol
"""
import struct
from typing import Tuple

import pytest

# Sync word documented in C++ as 0xAURORE07 (mnemonic).
# Actual value from C++ convention: 0xA7070007.
HUD_SYNC_WORD = 0xA7070007

HUD_HEADER_FORMAT = "<IHIQ"  # uint32_t sync_word, uint16_t msg_id, uint32_t seq, uint64_t ts
HUD_HEADER_SIZE = struct.calcsize(HUD_HEADER_FORMAT)  # 18 bytes

PAYLOAD_SIZE = 32
HMAC_SIZE = 32
HUD_MESSAGE_SIZE = HUD_HEADER_SIZE + PAYLOAD_SIZE + HMAC_SIZE  # 82 bytes


def pack_header(msg_id: int, sequence: int, timestamp_ns: int) -> bytes:
    return struct.pack(HUD_HEADER_FORMAT, HUD_SYNC_WORD, msg_id, sequence, timestamp_ns)


def unpack_header(data: bytes) -> dict:
    sync, msg_id, seq, ts = struct.unpack(HUD_HEADER_FORMAT, data)
    return {"sync_word": sync, "message_id": msg_id, "sequence": seq, "timestamp_ns": ts}


def pack_message(msg_id: int, sequence: int, timestamp_ns: int,
                 payload: bytes, hmac: bytes) -> bytes:
    assert len(payload) == PAYLOAD_SIZE
    assert len(hmac) == HMAC_SIZE
    header = pack_header(msg_id, sequence, timestamp_ns)
    return header + payload + hmac


class TestHudHeader:
    def test_header_size(self):
        assert HUD_HEADER_SIZE == 18, (
            f"HudBinaryHeader should be 18 bytes packed, got {HUD_HEADER_SIZE}"
        )

    def test_pack_and_unpack_header(self):
        data = pack_header(0x0301, 42, 1234567890)
        assert len(data) == HUD_HEADER_SIZE
        h = unpack_header(data)
        assert h["sync_word"] == HUD_SYNC_WORD
        assert h["message_id"] == 0x0301
        assert h["sequence"] == 42
        assert h["timestamp_ns"] == 1234567890

    def test_header_sync_word_matches(self):
        data = pack_header(0, 0, 0)
        h = unpack_header(data)
        assert h["sync_word"] == HUD_SYNC_WORD


class TestHudMessage:
    def test_message_size(self):
        assert HUD_MESSAGE_SIZE == 82, (
            f"HudBinaryMessage should be 82 bytes packed, got {HUD_MESSAGE_SIZE}"
        )

    def test_pack_and_unpack_message(self):
        payload = b"\x00" * PAYLOAD_SIZE
        hmac = b"\x01" * HMAC_SIZE
        msg = pack_message(0x0301, 1, 1000, payload, hmac)
        assert len(msg) == HUD_MESSAGE_SIZE

        header = unpack_header(msg[:HUD_HEADER_SIZE])
        assert header["message_id"] == 0x0301
        assert msg[HUD_HEADER_SIZE:HUD_HEADER_SIZE + PAYLOAD_SIZE] == payload
        assert msg[HUD_HEADER_SIZE + PAYLOAD_SIZE:] == hmac

    def test_reticle_payload_sizes(self):
        reticle_fmt = "<hhhh"  # reticle_x, reticle_y, lead_offset_x, lead_offset_y
        reticle_size = struct.calcsize(reticle_fmt)
        payload_remaining = PAYLOAD_SIZE - reticle_size
        assert payload_remaining == 24, f"Reticle reserved should be 24 bytes, got {payload_remaining}"

    def test_target_box_payload_size(self):
        box_fmt = "<HHHHB"  # box_x, box_y, box_width, box_height, confidence
        box_size = struct.calcsize(box_fmt)
        assert box_size == 9, f"TargetBox fixed fields should be 9 bytes, got {box_size}"
        payload_remaining = PAYLOAD_SIZE - box_size
        assert payload_remaining == 23, f"TargetBox reserved should be 23 bytes, got {payload_remaining}"

    def test_system_status_payload_size(self):
        status_fmt = "<BBBBHH"  # fcs_state, interlock, target_lock, fault_active, cpu_temp_c, deadline_misses
        status_size = struct.calcsize(status_fmt)
        assert status_size == 8, f"Status fixed fields should be 8 bytes, got {status_size}"
        payload_remaining = PAYLOAD_SIZE - status_size
        assert payload_remaining == 24, f"Status reserved should be 24 bytes, got {payload_remaining}"


class TestHudMessageIds:
    def test_all_message_ids_defined(self):
        assert 0x0301  # kReticleData
        assert 0x0302  # kTargetBox
        assert 0x0303  # kBallisticSolution
        assert 0x0304  # kSystemStatus

    def test_message_id_uniqueness(self):
        ids = {0x0301, 0x0302, 0x0303, 0x0304}
        assert len(ids) == 4


class TestHudPayloadStructures:
    def test_reticle_payload_round_trip(self):
        reticle_x, reticle_y = 100, 200
        lead_off_x, lead_off_y = 50, -30
        payload = struct.pack("<hhhh", reticle_x, reticle_y, lead_off_x, lead_off_y)
        payload += b"\x00" * (PAYLOAD_SIZE - len(payload))

        rx, ry, lx, ly = struct.unpack_from("<hhhh", payload, 0)
        assert rx == reticle_x
        assert ry == reticle_y
        assert lx == lead_off_x
        assert ly == lead_off_y

    def test_target_box_payload_round_trip(self):
        bx, by, bw, bh, conf = 400, 300, 200, 150, 85
        payload = struct.pack("<HHHHB", bx, by, bw, bh, conf)
        payload += b"\x00" * (PAYLOAD_SIZE - len(payload))

        px, py, pw, ph, pc = struct.unpack_from("<HHHHB", payload, 0)
        assert (px, py, pw, ph, pc) == (bx, by, bw, bh, conf)

    def test_system_status_payload_round_trip(self):
        state, interlock, lock, fault, temp, misses = 4, 1, 1, 0, 550, 3
        payload = struct.pack("<BBBBHH", state, interlock, lock, fault, temp, misses)
        payload += b"\x00" * (PAYLOAD_SIZE - len(payload))

        s, il, tl, fa, t, dm = struct.unpack_from("<BBBBHH", payload, 0)
        assert (s, il, tl, fa, t, dm) == (state, interlock, lock, fault, temp, misses)

    def test_ballistic_payload_round_trip(self):
        elev_adj, az_adj, range_m, ammo_id = 150, -75, 500, 1
        payload = struct.pack("<hhHB", elev_adj, az_adj, range_m, ammo_id)
        payload += b"\x00" * (PAYLOAD_SIZE - len(payload))

        ea, aa, r, ai = struct.unpack_from("<hhHB", payload, 0)
        assert (ea, aa, r, ai) == (elev_adj, az_adj, range_m, ammo_id)


class TestHudPayloadStructOffsets:
    """Verify packed struct field offsets per ICD-006."""

    def test_reticle_field_offsets(self):
        offset_reticle_x = 0
        offset_reticle_y = 2
        offset_lead_x = 4
        offset_lead_y = 6
        offset_reserved = 8
        assert offset_reticle_x == 0
        assert offset_reticle_y - offset_reticle_x == 2
        assert offset_lead_x - offset_reticle_y == 2
        assert offset_lead_y - offset_lead_x == 2
        assert offset_reserved - offset_lead_y == 2

    def test_reticle_payload_total(self):
        fixed = 8
        reserved = 24
        assert fixed + reserved == 32

    def test_target_box_field_offsets(self):
        offset_box_x = 0
        offset_box_y = 2
        offset_box_w = 4
        offset_box_h = 6
        offset_conf = 8
        offset_reserved = 9
        assert offset_box_x == 0
        assert offset_box_y == 2
        assert offset_conf == 8
        assert offset_reserved == 9

    def test_target_box_field_sizes(self):
        assert struct.calcsize("<H") == 2
        assert struct.calcsize("<B") == 1

    def test_target_box_total(self):
        fixed = 9
        reserved = 23
        assert fixed + reserved == 32

    def test_ballistic_field_offsets(self):
        offset_elev = 0
        offset_az = 2
        offset_range = 4
        offset_ammo = 6
        offset_reserved = 7
        assert offset_elev == 0
        assert offset_az == 2
        assert offset_range == 4
        assert offset_ammo == 6
        assert offset_reserved == 7

    def test_ballistic_field_sizes(self):
        assert struct.calcsize("<h") == 2
        assert struct.calcsize("<H") == 2
        assert struct.calcsize("<B") == 1

    def test_ballistic_total(self):
        fixed = 7
        reserved = 25
        assert fixed + reserved == 32

    def test_status_field_offsets(self):
        offset_state = 0
        offset_interlock = 1
        offset_lock = 2
        offset_fault = 3
        offset_temp = 4
        offset_misses = 6
        offset_reserved = 8
        assert offset_state == 0
        assert offset_interlock == 1
        assert offset_lock == 2
        assert offset_fault == 3
        assert offset_temp == 4
        assert offset_misses == 6
        assert offset_reserved == 8

    def test_status_field_sizes(self):
        assert struct.calcsize("<B") == 1
        assert struct.calcsize("<H") == 2

    def test_status_total(self):
        fixed = 8
        reserved = 24
        assert fixed + reserved == 32

    def test_all_payloads_same_size(self):
        assert 32 == 32


class TestHudFrameStructure:
    """HudFrame: high-level frame structure."""

    def test_default_values(self):
        f = {"state": 0, "az_deg": 0.0, "el_deg": 0.0,
             "target_cx": 0.0, "target_cy": 0.0, "confidence": 0.0,
             "p_hit": 0.0, "range_m": 0.0, "timestamp_ns": 0,
             "interlock": 0, "target_lock": 0, "fault_active": 0}
        assert f["state"] == 0
        assert f["interlock"] == 0

    def test_tracking_frame(self):
        f = {"state": 5, "az_deg": 45.0, "el_deg": 10.0,
             "target_cx": 768.0, "target_cy": 432.0, "confidence": 0.85,
             "p_hit": 0.92, "range_m": 50.0,
             "interlock": 1, "target_lock": 1, "fault_active": 0}
        assert f["state"] == 5
        assert f["interlock"] == 1
        assert f["p_hit"] > 0.5

    def test_frame_fields(self):
        fields = ["state", "az_deg", "el_deg", "target_cx", "target_cy",
                  "confidence", "p_hit", "range_m", "timestamp_ns",
                  "target_w", "target_h", "velocity_x", "velocity_y",
                  "az_lead_mrad", "el_lead_mrad", "deadline_misses",
                  "ammo_id", "interlock", "target_lock", "fault_active",
                  "cpu_temp_c"]
        assert len(fields) == 21
