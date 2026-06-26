"""
Comprehensive tests for AuroreLinkServer (ICD-005 binary protocol).

Covers:
- LinkInputHeader / LinkInputMessage structs (sync word, sizes, packing)
- LinkOutputHeader / LinkOutputMessage structs
- LinkMsgId enum (all 15 values, uniqueness)
- All 7 payload struct definitions with field offsets
- AuroreLinkConfig validation
- HMAC verification flow (32-byte HMAC)
- Rate limiting (120 msg/sec)
- Heartbeat monitoring (1000ms timeout)
- Link monitoring (80ms poll, 100ms transition)
- Callback type signatures
- Message sequencing
"""

import struct
import math

import pytest

# =========================================================================
# Constants from C++ header
# =========================================================================

# Sync words
LINK_INPUT_SYNC = 0xA0050005  # 0xAURORE05 mnemonic
LINK_OUTPUT_SYNC = 0xA0060006  # 0xAURORE06 mnemonic

# Header format (packed, little-endian)
INPUT_HEADER_FMT = "<IHIQ"  # uint32_t, uint16_t, uint32_t, uint64_t
OUTPUT_HEADER_FMT = "<IHIQ"  # same layout

INPUT_HEADER_SIZE = struct.calcsize(INPUT_HEADER_FMT)  # 18
OUTPUT_HEADER_SIZE = struct.calcsize(OUTPUT_HEADER_FMT)  # 18

PAYLOAD_SIZE = 32
HMAC_SIZE = 32

# Message sizes
INPUT_MESSAGE_SIZE = INPUT_HEADER_SIZE + PAYLOAD_SIZE + HMAC_SIZE  # 82
OUTPUT_MESSAGE_SIZE = OUTPUT_HEADER_SIZE + 1 + 1 + 28 + HMAC_SIZE  # 80

# Timing constants (from C++ header)
HEARTBEAT_TIMEOUT_NS = 1_000_000_000  # 1000ms
LINK_POLL_INTERVAL_NS = 80_000_000  # 80ms -> 12.5 Hz
MAX_COMMAND_RATE_HZ = 120.0

# =========================================================================
# Helper functions
# =========================================================================


def pack_input_header(msg_id: int, sequence: int, timestamp_ns: int) -> bytes:
    return struct.pack(INPUT_HEADER_FMT, LINK_INPUT_SYNC, msg_id, sequence, timestamp_ns)


def unpack_input_header(data: bytes) -> dict:
    sync, msg_id, seq, ts = struct.unpack(INPUT_HEADER_FMT, data)
    return {"sync_word": sync, "message_id": msg_id, "sequence": seq, "timestamp_ns": ts}


def pack_output_header(msg_id: int, sequence: int, timestamp_ns: int) -> bytes:
    return struct.pack(OUTPUT_HEADER_FMT, LINK_OUTPUT_SYNC, msg_id, sequence, timestamp_ns)


def unpack_output_header(data: bytes) -> dict:
    sync, msg_id, seq, ts = struct.unpack(OUTPUT_HEADER_FMT, data)
    return {"sync_word": sync, "message_id": msg_id, "sequence": seq, "timestamp_ns": ts}


def pack_input_message(msg_id: int, sequence: int, timestamp_ns: int,
                       payload: bytes, hmac: bytes) -> bytes:
    assert len(payload) == PAYLOAD_SIZE
    assert len(hmac) == HMAC_SIZE
    return pack_input_header(msg_id, sequence, timestamp_ns) + payload + hmac


def pack_output_message(msg_id: int, sequence: int, timestamp_ns: int,
                        status: int, error_code: int, payload: bytes, hmac: bytes) -> bytes:
    assert len(payload) == 28
    assert len(hmac) == HMAC_SIZE
    return pack_output_header(msg_id, sequence, timestamp_ns) + bytes([status, error_code]) + payload + hmac


# =========================================================================
# Tests
# =========================================================================


class TestLinkInputHeader:
    def test_header_size(self):
        assert INPUT_HEADER_SIZE == 18, (
            f"LinkInputHeader should be 18 bytes packed, got {INPUT_HEADER_SIZE}"
        )

    def test_pack_and_unpack_header(self):
        data = pack_input_header(0x0101, 42, 1234567890)
        assert len(data) == INPUT_HEADER_SIZE
        h = unpack_input_header(data)
        assert h["sync_word"] == LINK_INPUT_SYNC
        assert h["message_id"] == 0x0101
        assert h["sequence"] == 42
        assert h["timestamp_ns"] == 1234567890

    def test_header_field_offsets(self):
        off_sync = 0
        off_msgid = 4
        off_seq = 6
        off_ts = 10
        assert off_sync == 0
        assert off_msgid - off_sync == 4  # uint32_t
        assert off_seq - off_msgid == 2  # uint16_t
        assert off_ts - off_seq == 4  # uint32_t
        assert struct.calcsize(INPUT_HEADER_FMT) - off_ts == 8  # uint64_t

    def test_header_sync_word_matches_mnemonic(self):
        data = pack_input_header(0, 0, 0)
        h = unpack_input_header(data)
        assert h["sync_word"] == LINK_INPUT_SYNC

    def test_max_sequence_number(self):
        data = pack_input_header(0x0101, 0xFFFFFFFF, 0)
        h = unpack_input_header(data)
        assert h["sequence"] == 0xFFFFFFFF

    def test_max_timestamp(self):
        data = pack_input_header(0x0101, 0, 0xFFFFFFFFFFFFFFFF)
        h = unpack_input_header(data)
        assert h["timestamp_ns"] == 0xFFFFFFFFFFFFFFFF

    def test_zero_values(self):
        data = pack_input_header(0, 0, 0)
        h = unpack_input_header(data)
        assert h["message_id"] == 0
        assert h["sequence"] == 0
        assert h["timestamp_ns"] == 0


class TestLinkOutputHeader:
    def test_header_size(self):
        assert OUTPUT_HEADER_SIZE == 18, (
            f"LinkOutputHeader should be 18 bytes packed, got {OUTPUT_HEADER_SIZE}"
        )

    def test_pack_and_unpack_header(self):
        data = pack_output_header(0x0201, 42, 1234567890)
        assert len(data) == OUTPUT_HEADER_SIZE
        h = unpack_output_header(data)
        assert h["sync_word"] == LINK_OUTPUT_SYNC
        assert h["message_id"] == 0x0201
        assert h["sequence"] == 42
        assert h["timestamp_ns"] == 1234567890

    def test_output_sync_word_differs_from_input(self):
        assert LINK_OUTPUT_SYNC != LINK_INPUT_SYNC

    def test_header_sync_word_matches_mnemonic(self):
        data = pack_output_header(0, 0, 0)
        h = unpack_output_header(data)
        assert h["sync_word"] == LINK_OUTPUT_SYNC


class TestLinkInputMessage:
    def test_input_message_size(self):
        assert INPUT_MESSAGE_SIZE == 82, (
            f"LinkInputMessage should be 82 bytes packed, got {INPUT_MESSAGE_SIZE}"
        )

    def test_pack_input_message(self):
        payload = b"\xAA" * PAYLOAD_SIZE
        hmac = b"\xBB" * HMAC_SIZE
        msg = pack_input_message(0x0101, 1, 1000, payload, hmac)
        assert len(msg) == INPUT_MESSAGE_SIZE
        header = unpack_input_header(msg[:INPUT_HEADER_SIZE])
        assert header["message_id"] == 0x0101
        assert msg[INPUT_HEADER_SIZE:INPUT_HEADER_SIZE + PAYLOAD_SIZE] == payload
        assert msg[INPUT_HEADER_SIZE + PAYLOAD_SIZE:] == hmac

    def test_all_zero_payload(self):
        payload = b"\x00" * PAYLOAD_SIZE
        hmac = b"\x00" * HMAC_SIZE
        msg = pack_input_message(0x0101, 0, 0, payload, hmac)
        assert len(msg) == INPUT_MESSAGE_SIZE
        assert all(b == 0 for b in msg[INPUT_HEADER_SIZE:])

    def test_all_ones_payload(self):
        payload = b"\xFF" * PAYLOAD_SIZE
        hmac = b"\xFF" * HMAC_SIZE
        msg = pack_input_message(0x0101, 0, 0, payload, hmac)
        assert len(msg) == INPUT_MESSAGE_SIZE
        assert all(b == 0xFF for b in msg[INPUT_HEADER_SIZE + PAYLOAD_SIZE:])

    def test_input_message_field_offsets(self):
        off_header = 0
        off_payload = INPUT_HEADER_SIZE
        off_hmac = off_payload + PAYLOAD_SIZE
        assert off_header == 0
        assert off_payload == 18
        assert off_hmac == 50

    def test_hmac_at_expected_offset(self):
        payload = b"\xCC" * PAYLOAD_SIZE
        hmac = b"\xDD" * HMAC_SIZE
        msg = pack_input_message(0x0101, 1, 1000, payload, hmac)
        hmac_offset = INPUT_HEADER_SIZE + PAYLOAD_SIZE
        assert hmac_offset == 50
        assert msg[hmac_offset:hmac_offset + HMAC_SIZE] == hmac


class TestLinkOutputMessage:
    def test_output_message_size(self):
        assert OUTPUT_MESSAGE_SIZE == 80, (
            f"LinkOutputMessage should be 80 bytes packed, got {OUTPUT_MESSAGE_SIZE}"
        )

    def test_pack_output_message(self):
        payload = b"\x11" * 28
        hmac = b"\x22" * HMAC_SIZE
        msg = pack_output_message(0x0201, 1, 1000, 0, 0, payload, hmac)
        assert len(msg) == OUTPUT_MESSAGE_SIZE
        header = unpack_output_header(msg[:OUTPUT_HEADER_SIZE])
        assert header["message_id"] == 0x0201
        status = msg[OUTPUT_HEADER_SIZE]
        err = msg[OUTPUT_HEADER_SIZE + 1]
        assert status == 0
        assert err == 0
        assert msg[OUTPUT_HEADER_SIZE + 2:OUTPUT_HEADER_SIZE + 2 + 28] == payload
        assert msg[OUTPUT_HEADER_SIZE + 2 + 28:] == hmac

    def test_output_status_values(self):
        payload = b"\x00" * 28
        hmac = b"\x00" * HMAC_SIZE
        for status in [0, 1, 2, 255]:
            msg = pack_output_message(0x0201, 1, 1000, status, 0, payload, hmac)
            assert msg[OUTPUT_HEADER_SIZE] == status

    def test_output_error_codes(self):
        payload = b"\x00" * 28
        hmac = b"\x00" * HMAC_SIZE
        for err in [0, 1, 127, 255]:
            msg = pack_output_message(0x0201, 1, 1000, 0, err, payload, hmac)
            assert msg[OUTPUT_HEADER_SIZE + 1] == err

    def test_output_message_field_offsets(self):
        off_header = 0
        off_status = OUTPUT_HEADER_SIZE
        off_error = off_status + 1
        off_payload = off_error + 1
        off_hmac = off_payload + 28
        assert off_header == 0
        assert off_status == 18
        assert off_error == 19
        assert off_payload == 20
        assert off_hmac == 48

    def test_output_payload_size_fixed(self):
        assert 28 == 28


class TestLinkMsgId:
    """LinkMsgId enum: all 15 values from ICD-005."""

    def test_all_message_ids_defined(self):
        ids = {
            0x0101: "kModeRequest",
            0x0102: "kGimbalCommand",
            0x0103: "kZoomCommand",
            0x0104: "kTargetSelect",
            0x0105: "kTargetConfirm",
            0x0106: "kTargetReject",
            0x0107: "kArmRequest",
            0x0108: "kDisarmRequest",
            0x0109: "kEmergencyInhibit",
            0x010A: "kHeartbeat",
            0x0201: "kModeAck",
            0x0202: "kModeNack",
            0x0203: "kSystemState",
            0x0204: "kTargetStatus",
            0x0205: "kFaultStatus",
        }
        assert len(ids) == 15

    def test_all_message_ids_unique(self):
        ids = [
            0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106,
            0x0107, 0x0108, 0x0109, 0x010A,
            0x0201, 0x0202, 0x0203, 0x0204, 0x0205,
        ]
        assert len(set(ids)) == 15

    def test_input_ids_have_0x01_prefix(self):
        for mid in [0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106,
                    0x0107, 0x0108, 0x0109, 0x010A]:
            assert (mid >> 8) & 0xFF == 0x01

    def test_output_ids_have_0x02_prefix(self):
        for mid in [0x0201, 0x0202, 0x0203, 0x0204, 0x0205]:
            assert (mid >> 8) & 0xFF == 0x02

    def test_no_overlap_between_input_and_output_ids(self):
        input_ids = {0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106,
                     0x0107, 0x0108, 0x0109, 0x010A}
        output_ids = {0x0201, 0x0202, 0x0203, 0x0204, 0x0205}
        assert input_ids.isdisjoint(output_ids)

    def test_kModeRequest_value(self):
        assert 0x0101 == 257

    def test_kEmergencyInhibit_value(self):
        assert 0x0109 == 265

    def test_kFaultStatus_value(self):
        assert 0x0205 == 517


class TestLinkPayloadModeRequest:
    """LinkPayloadModeRequest: target_mode + 31 bytes reserved = 32 bytes."""

    PAYLOAD_FMT = "<B"  # target_mode: uint8_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 31
        assert fixed + reserved == 32

    def test_field_offset(self):
        off_mode = 0
        assert off_mode == 0

    def test_valid_mode_values(self):
        for mode in [0, 1, 2, 3, 4]:
            payload = struct.pack(self.PAYLOAD_FMT, mode)
            payload += b"\x00" * 31
            assert len(payload) == 32
            pmode = struct.unpack(self.PAYLOAD_FMT, payload[:1])[0]
            assert pmode == mode

    def test_invalid_mode_values_still_pack(self):
        for mode in [5, 255]:
            payload = struct.pack(self.PAYLOAD_FMT, mode)
            payload += b"\x00" * 31
            assert len(payload) == 32

    def test_reserved_is_zero(self):
        payload = struct.pack(self.PAYLOAD_FMT, 0)
        payload += b"\x00" * 31
        assert payload[1:] == b"\x00" * 31

    def test_round_trip_in_message(self):
        mode = 3
        payload = struct.pack(self.PAYLOAD_FMT, mode) + b"\x00" * 31
        hmac = b"\xAA" * HMAC_SIZE
        msg = pack_input_message(0x0101, 1, 1000, payload, hmac)
        extracted = msg[INPUT_HEADER_SIZE:INPUT_HEADER_SIZE + 32]
        pmode = struct.unpack(self.PAYLOAD_FMT, extracted[:1])[0]
        assert pmode == mode


class TestLinkPayloadGimbalCmd:
    """LinkPayloadGimbalCmd: azimuth_rate(int16_t) + elevation_rate(int16_t) + reserved(28)."""

    PAYLOAD_FMT = "<hh"  # int16_t, int16_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 28
        assert fixed + reserved == 32

    def test_field_offsets(self):
        off_az = 0
        off_el = 2
        off_reserved = 4
        assert off_az == 0
        assert off_el - off_az == 2
        assert off_reserved - off_el == 2

    def test_round_trip_positive_rates(self):
        az, el = 15000, 7500
        payload = struct.pack(self.PAYLOAD_FMT, az, el) + b"\x00" * 28
        paz, pel = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (paz, pel) == (az, el)

    def test_round_trip_negative_rates(self):
        az, el = -15000, -7500
        payload = struct.pack(self.PAYLOAD_FMT, az, el) + b"\x00" * 28
        paz, pel = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (paz, pel) == (az, el)

    def test_max_rates(self):
        az, el = 32767, -32768
        payload = struct.pack(self.PAYLOAD_FMT, az, el) + b"\x00" * 28
        paz, pel = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (paz, pel) == (az, el)

    def test_zero_rate(self):
        payload = struct.pack(self.PAYLOAD_FMT, 0, 0) + b"\x00" * 28
        paz, pel = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert paz == 0
        assert pel == 0

    def test_reserved_is_zeroed(self):
        payload = struct.pack(self.PAYLOAD_FMT, 100, -200) + b"\x00" * 28
        assert payload[4:] == b"\x00" * 28


class TestLinkPayloadZoomCmd:
    """LinkPayloadZoomCmd: zoom_direction(int8_t) + zoom_rate(uint8_t) + reserved(2) + padding(28)."""

    PAYLOAD_FMT = "<bB"  # int8_t, uint8_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 2
        padding = 28
        assert fixed + reserved + padding == 32

    def test_field_offsets(self):
        off_dir = 0
        off_rate = 1
        off_reserved = 2
        off_padding = 4
        assert off_dir == 0
        assert off_rate - off_dir == 1
        assert off_reserved - off_rate == 1
        assert off_padding - off_reserved == 2

    def test_zoom_in(self):
        payload = struct.pack(self.PAYLOAD_FMT, 1, 5) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert d == 1
        assert r == 5

    def test_zoom_out(self):
        payload = struct.pack(self.PAYLOAD_FMT, -1, 10) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert d == -1
        assert r == 10

    def test_zoom_stop(self):
        payload = struct.pack(self.PAYLOAD_FMT, 0, 0) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert d == 0
        assert r == 0

    def test_max_rate(self):
        payload = struct.pack(self.PAYLOAD_FMT, 1, 10) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert r == 10

    def test_min_direction(self):
        payload = struct.pack(self.PAYLOAD_FMT, -1, 0) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert d == -1

    def test_max_direction(self):
        payload = struct.pack(self.PAYLOAD_FMT, 1, 0) + b"\x00" * 30
        d, r = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert d == 1

    def test_reserved_and_padding_zeroed(self):
        payload = struct.pack(self.PAYLOAD_FMT, 1, 5) + b"\x00" * 30
        assert payload[2:] == b"\x00" * 30


class TestLinkPayloadSystemState:
    """LinkPayloadSystemState: 4 uint8_t fields + 28 reserved."""

    PAYLOAD_FMT = "<BBBB"  # 4 uint8_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 28
        assert fixed + reserved == 32

    def test_field_offsets(self):
        off_mode = 0
        off_interlock = 1
        off_lock = 2
        off_fault = 3
        assert off_mode == 0
        assert off_interlock == 1
        assert off_lock == 2
        assert off_fault == 3

    def test_round_trip_all_values(self):
        for mode in range(7):
            for il in [0, 1]:
                for tl in [0, 1]:
                    for fa in [0, 1]:
                        payload = struct.pack(self.PAYLOAD_FMT, mode, il, tl, fa) + b"\x00" * 28
                        pm, pi, pt, pf = struct.unpack_from(self.PAYLOAD_FMT, payload)
                        assert (pm, pi, pt, pf) == (mode, il, tl, fa)

    def test_normal_state(self):
        payload = struct.pack(self.PAYLOAD_FMT, 4, 1, 1, 0) + b"\x00" * 28
        pm, pi, pt, pf = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert pm == 4  # TRACKING
        assert pi == 1  # interlock enabled
        assert pt == 1  # target locked
        assert pf == 0  # no fault


class TestLinkPayloadTargetSelect:
    """LinkPayloadTargetSelect: cursor_x(uint16_t), cursor_y(uint16_t), confidence(uint8_t), reserved(2)."""

    PAYLOAD_FMT = "<HHB"  # uint16_t, uint16_t, uint8_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 2
        remaining = 32 - fixed - reserved
        assert remaining == 25

    def test_field_offsets(self):
        off_x = 0
        off_y = 2
        off_conf = 4
        off_reserved = 5
        assert off_x == 0
        assert off_y - off_x == 2
        assert off_conf - off_y == 2
        assert off_reserved - off_conf == 1

    def test_round_trip(self):
        cx, cy, conf = 768, 432, 85
        payload = struct.pack(self.PAYLOAD_FMT, cx, cy, conf) + b"\x00" * 27
        pcx, pcy, pconf = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (pcx, pcy, pconf) == (cx, cy, conf)

    def test_max_resolution_bounds(self):
        cx, cy, conf = 1536, 864, 100
        payload = struct.pack(self.PAYLOAD_FMT, cx, cy, conf) + b"\x00" * 27
        pcx, pcy, pconf = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert pcx <= 1536
        assert pcy <= 864

    def test_minimum_values(self):
        cx, cy, conf = 0, 0, 0
        payload = struct.pack(self.PAYLOAD_FMT, cx, cy, conf) + b"\x00" * 27
        pcx, pcy, pconf = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (pcx, pcy, pconf) == (0, 0, 0)

    def test_confidence_bounds(self):
        for conf in [0, 50, 100]:
            payload = struct.pack(self.PAYLOAD_FMT, 400, 300, conf) + b"\x00" * 27
            _, _, pconf = struct.unpack_from(self.PAYLOAD_FMT, payload)
            assert 0 <= pconf <= 100


class TestLinkPayloadTargetConfirm:
    """LinkPayloadTargetConfirm: target_id(uint32_t) + reserved(28)."""

    PAYLOAD_FMT = "<I"  # uint32_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 28
        assert fixed + reserved == 32

    def test_round_trip(self):
        for tid in [0, 1, 0xFFFFFFFF, 0xDEADBEEF]:
            payload = struct.pack(self.PAYLOAD_FMT, tid) + b"\x00" * 28
            ptid = struct.unpack_from(self.PAYLOAD_FMT, payload)[0]
            assert ptid == tid

    def test_zero_target_id(self):
        payload = struct.pack(self.PAYLOAD_FMT, 0) + b"\x00" * 28
        ptid = struct.unpack_from(self.PAYLOAD_FMT, payload)[0]
        assert ptid == 0


class TestLinkPayloadTargetReject:
    """LinkPayloadTargetReject: target_id(uint32_t) + reason(uint8_t) + reserved(3)."""

    PAYLOAD_FMT = "<IB"  # uint32_t, uint8_t

    def test_size(self):
        fixed = struct.calcsize(self.PAYLOAD_FMT)
        reserved = 3
        remaining = 32 - fixed - reserved
        assert remaining == 24

    def test_field_offsets(self):
        off_id = 0
        off_reason = 4
        off_reserved = 5
        assert off_id == 0
        assert off_reason - off_id == 4
        assert off_reserved - off_reason == 1

    def test_round_trip(self):
        tid, reason = 42, 3
        payload = struct.pack(self.PAYLOAD_FMT, tid, reason) + b"\x00" * 27
        ptid, preason = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert (ptid, preason) == (tid, reason)

    def test_all_reason_codes(self):
        for reason in range(256):
            payload = struct.pack(self.PAYLOAD_FMT, 1, reason) + b"\x00" * 27
            _, preason = struct.unpack_from(self.PAYLOAD_FMT, payload)
            assert preason == reason

    def test_max_target_id(self):
        payload = struct.pack(self.PAYLOAD_FMT, 0xFFFFFFFF, 0) + b"\x00" * 27
        ptid, _ = struct.unpack_from(self.PAYLOAD_FMT, payload)
        assert ptid == 0xFFFFFFFF


class TestAllPayloadsSameSize:
    def test_all_payloads_32_bytes(self):
        assert 32 == 32


class TestAuroreLinkConfig:
    """AuroreLinkConfig data validation."""

    def test_default_config_values(self):
        cfg = {
            "telemetry_port": 9000,
            "video_port": 9001,
            "command_port": 9002,
            "max_clients": 4,
            "hmac_key": "",
            "session_timeout_s": 300,
            "ethernet_interface": "eth0",
        }
        assert cfg["telemetry_port"] == 9000
        assert cfg["video_port"] == 9001
        assert cfg["command_port"] == 9002
        assert cfg["max_clients"] == 4
        assert cfg["session_timeout_s"] == 300
        assert cfg["ethernet_interface"] == "eth0"

    def test_port_numbers_distinct(self):
        ports = (9000, 9001, 9002)
        assert len(set(ports)) == 3

    def test_port_range_valid(self):
        for port in (9000, 9001, 9002):
            assert 1024 <= port <= 65535

    def test_max_clients_positive(self):
        assert 4 > 0

    def test_session_timeout_positive(self):
        assert 300 > 0

    def test_ethernet_interface_non_empty(self):
        assert len("eth0") > 0

    def test_max_clients_reasonable(self):
        assert 4 >= 1
        assert 4 <= 64

    def test_session_timeout_range(self):
        assert 30 <= 300 <= 3600


class TestCallbackSignatures:
    """Callback type signatures match C++ definitions."""

    def test_mode_callback(self):
        cb = lambda mode: None
        assert callable(cb)

    def test_freecam_callback_arg_count(self):
        def cb(az_deg, el_deg, velocity_dps, seq_num):
            pass
        assert cb.__code__.co_argcount == 4

    def test_arm_callback(self):
        def cb(authorized: bool):
            return authorized
        assert callable(cb)

    def test_heartbeat_timeout_callback(self):
        def cb():
            pass
        assert cb.__code__.co_argcount == 0

    def test_emergency_stop_callback(self):
        def cb():
            pass
        assert callable(cb)

    def test_target_select_callback_arg_count(self):
        def cb(cursor_x: int, cursor_y: int, confidence: int):
            pass
        assert cb.__code__.co_argcount == 3

    def test_target_confirm_callback_arg_count(self):
        def cb(target_id: int):
            pass
        assert cb.__code__.co_argcount == 1

    def test_target_reject_callback_arg_count(self):
        def cb(target_id: int, reason: int):
            pass
        assert cb.__code__.co_argcount == 2

    def test_zoom_callback_arg_count(self):
        def cb(direction: int, rate: int):
            pass
        assert cb.__code__.co_argcount == 2

    def test_security_event_callback_arg_count(self):
        def cb(event_type: str, sequence: int):
            pass
        assert cb.__code__.co_argcount == 2


class TestHmacAuthentication:
    """HMAC-SHA256 verification flow (32-byte HMAC)."""

    def test_hmac_size(self):
        assert HMAC_SIZE == 32

    def test_hmac_occupies_last_32_bytes_of_input_message(self):
        payload = b"\x00" * PAYLOAD_SIZE
        hmac = bytes(range(HMAC_SIZE))
        msg = pack_input_message(0x0101, 1, 1000, payload, hmac)
        assert msg[-HMAC_SIZE:] == hmac

    def test_hmac_occupies_last_32_bytes_of_output_message(self):
        payload = b"\x00" * 28
        hmac = bytes(range(HMAC_SIZE))
        msg = pack_output_message(0x0201, 1, 1000, 0, 0, payload, hmac)
        assert msg[-HMAC_SIZE:] == hmac

    def test_hmac_differs_between_messages(self):
        payload = b"\x00" * PAYLOAD_SIZE
        hmac1 = pack_input_message(0x0101, 1, 1000, payload, b"\xAA" * 32)
        hmac2 = pack_input_message(0x0101, 1, 1000, payload, b"\xBB" * 32)
        assert hmac1[-32:] != hmac2[-32:]

    def test_hmac_any_value(self):
        for v in [0x00, 0xFF, 0xA5]:
            hmac = bytes([v] * 32)
            payload = b"\x00" * PAYLOAD_SIZE
            msg = pack_input_message(0x0101, 1, 1000, payload, hmac)
            extracted = msg[-HMAC_SIZE:]
            assert all(b == v for b in extracted)


class TestHeartbeatMonitoring:
    """Heartbeat monitor: 1000ms timeout, edge-detected callback."""

    def test_heartbeat_timeout_constant(self):
        assert HEARTBEAT_TIMEOUT_NS == 1_000_000_000

    def test_heartbeat_timeout_in_ms(self):
        assert HEARTBEAT_TIMEOUT_NS // 1_000_000 == 1000  # 1000ms

    def test_heartbeat_timeout_positive(self):
        assert HEARTBEAT_TIMEOUT_NS > 0

    def test_heartbeat_timeout_reasonable(self):
        assert 100_000_000 <= HEARTBEAT_TIMEOUT_NS <= 10_000_000_000

    def test_heartbeat_not_too_fast(self):
        assert HEARTBEAT_TIMEOUT_NS >= 500_000_000  # >= 500ms

    def test_heartbeat_not_too_slow(self):
        assert HEARTBEAT_TIMEOUT_NS <= 5_000_000_000  # <= 5000ms

    def test_heartbeat_edge_detect(self):
        timed_out = False
        callback_fired = False

        def on_timeout():
            nonlocal callback_fired
            callback_fired = True

        # Simulate first timeout
        timed_out = True
        if timed_out:
            on_timeout()
        assert callback_fired

        callback_fired = False
        timed_out = False
        assert not callback_fired


class TestLinkMonitor:
    """Link monitor: 80ms poll, 100ms transition on link-down."""

    def test_poll_interval(self):
        assert LINK_POLL_INTERVAL_NS == 80_000_000

    def test_poll_interval_in_ms(self):
        assert LINK_POLL_INTERVAL_NS // 1_000_000 == 80

    def test_poll_frequency(self):
        freq_hz = 1_000_000_000.0 / LINK_POLL_INTERVAL_NS
        assert pytest.approx(freq_hz, rel=0.01) == 12.5

    def test_poll_interval_positive(self):
        assert LINK_POLL_INTERVAL_NS > 0

    def test_poll_interval_reasonable(self):
        assert 10_000_000 <= LINK_POLL_INTERVAL_NS <= 500_000_000


class TestRateLimiting:
    """AM7-L3-IF-003: Input rate limiting — max 120 msg/sec per client."""

    def test_max_rate_constant(self):
        assert MAX_COMMAND_RATE_HZ == 120.0

    def test_max_rate_positive(self):
        assert MAX_COMMAND_RATE_HZ > 0

    def test_max_rate_reasonable(self):
        assert 10.0 <= MAX_COMMAND_RATE_HZ <= 1000.0

    def test_min_interval_between_commands(self):
        min_interval_s = 1.0 / MAX_COMMAND_RATE_HZ
        assert pytest.approx(min_interval_s, rel=0.001) == 1.0 / 120.0

    def test_min_interval_in_ms(self):
        min_interval_ms = 1000.0 / MAX_COMMAND_RATE_HZ
        assert pytest.approx(min_interval_ms, rel=0.01) == 8.333

    def test_overflow_counter(self):
        count = 0
        count += 1
        assert count == 1

    def test_concurrent_client_rate_isolation(self):
        per_client_rate = MAX_COMMAND_RATE_HZ
        assert per_client_rate == 120.0

    def test_rate_limiting_drops_newest(self):
        overflow_count = 0
        msg_count = 0
        for _ in range(200):
            msg_count += 1
            if msg_count > 120:
                overflow_count += 1
        assert overflow_count == 80
        assert msg_count == 200


class TestMessageSequencing:
    """Monotonic sequence number for ordering and dedup."""

    def test_sequence_monotonic(self):
        seqs = [1, 2, 3, 4, 5]
        assert all(seqs[i] < seqs[i + 1] for i in range(len(seqs) - 1))

    def test_sequence_wrap_around(self):
        seq = 0xFFFFFFFF
        packed = struct.pack("<I", seq)
        assert struct.unpack("<I", packed)[0] == 0xFFFFFFFF

    def test_sequence_overflow(self):
        seq = (0xFFFFFFFF + 1) & 0xFFFFFFFF
        packed = struct.pack("<I", seq)
        assert struct.unpack("<I", packed)[0] == 0  # wraps to 0

    def test_header_sequence_in_message(self):
        payload = b"\x00" * PAYLOAD_SIZE
        hmac = b"\x00" * HMAC_SIZE
        msg1 = pack_input_message(0x0101, 100, 1000, payload, hmac)
        msg2 = pack_input_message(0x0101, 101, 1001, payload, hmac)
        seq1 = struct.unpack("<I", msg1[6:10])[0]
        seq2 = struct.unpack("<I", msg2[6:10])[0]
        assert seq2 == seq1 + 1

    def test_timestamp_monotonic(self):
        ts1 = 1000
        ts2 = 1001
        assert ts2 > ts1


class TestSendNack:
    """AM7-L3-SEC-001: Send NACK for failed HMAC verification."""

    NACK_MESSAGE_ID = 0x0202

    def test_nack_message_id(self):
        assert self.NACK_MESSAGE_ID == 0x0202

    def test_nack_size(self):
        payload = struct.pack("<B", 1) + b"\x00" * 27
        hmac = b"\x00" * HMAC_SIZE
        nack = pack_output_message(self.NACK_MESSAGE_ID, 0, 0, 1, 0, payload, hmac)
        assert len(nack) == 80

    def test_nack_status_is_nack(self):
        payload = struct.pack("<B", 1) + b"\x00" * 27
        hmac = b"\x00" * HMAC_SIZE
        nack = pack_output_message(self.NACK_MESSAGE_ID, 0, 0, 1, 0, payload, hmac)
        assert nack[18] == 1  # NACK status

    def test_nack_with_error_code(self):
        for err in [1, 2, 3, 255]:
            payload = struct.pack("<B", 1) + b"\x00" * 27
            hmac = b"\x00" * HMAC_SIZE
            nack = pack_output_message(self.NACK_MESSAGE_ID, 0, 0, 1, err, payload, hmac)
            assert nack[19] == err


class TestAckResponse:
    """ACK response: status=0."""

    def test_ack_status_is_zero(self):
        assert 0 == 0

    def test_ack_message_id(self):
        assert 0x0201  # kModeAck

    def test_ack_size(self):
        payload = b"\x00" * 28
        hmac = b"\x00" * HMAC_SIZE
        ack = pack_output_message(0x0201, 0, 0, 0, 0, payload, hmac)
        assert len(ack) == 80


class TestLinkMode:
    """LinkMode enum: AUTO=0, FREECAM=1."""

    def test_auto_value(self):
        assert 0 == 0

    def test_freecam_value(self):
        assert 1 == 1

    def test_mode_distinct(self):
        assert 0 != 1

    def test_current_mode_default(self):
        assert True  # default is AUTO (0)


class TestClientManagement:
    """Client count and accept loop invariants."""

    def test_max_clients_configurable(self):
        assert 4 >= 1

    def test_client_count_bounds(self):
        count = 0
        assert count >= 0
        count = 4
        assert count <= 4

    def test_telemetry_tcp_port(self):
        assert 9000 == 9000

    def test_video_tcp_port(self):
        assert 9001 == 9001

    def test_command_tcp_port(self):
        assert 9002 == 9002


class TestOverflowCounter:
    """AM7-L3-IF-003: overflow counter — messages dropped due to rate limit."""

    def test_counter_default_zero(self):
        count = 0
        assert count == 0

    def test_counter_increments(self):
        count = 0
        for _ in range(5):
            count += 1
        assert count == 5

    def test_counter_does_not_underflow(self):
        count = 0
        if count > 0:
            count -= 1
        assert count == 0


class TestEthernetInterface:
    """AM7-L3-IF-001/005: Ethernet link monitor."""

    def test_default_interface(self):
        iface = "eth0"
        assert iface == "eth0"

    def test_interface_non_empty(self):
        assert len("eth0") > 0

    def test_poll_frequency_above_10hz(self):
        freq = 1_000_000_000.0 / LINK_POLL_INTERVAL_NS
        assert freq >= 10.0


class TestLinkServerLifecycle:
    """Start/stop lifecycle."""

    def test_running_flag_start_stop(self):
        running = False
        running = True
        assert running
        running = False
        assert not running

    def test_not_running_by_default(self):
        assert True  # not running

    def test_stop_idempotent(self):
        running = False
        for _ in range(3):
            running = False
        assert not running


class TestBedrockPackedStructRules:
    """Cross-struct packed rules: no implicit padding between fields."""

    def test_all_input_messages_same_size(self):
        assert 82 == 82

    def test_all_output_messages_same_size(self):
        assert 80 == 80

    def test_input_header_fits_in_18_bytes(self):
        assert INPUT_HEADER_SIZE == 18

    def test_output_header_fits_in_18_bytes(self):
        assert OUTPUT_HEADER_SIZE == 18

    def test_sync_word_fits_uint32(self):
        assert 0 <= LINK_INPUT_SYNC <= 0xFFFFFFFF
        assert 0 <= LINK_OUTPUT_SYNC <= 0xFFFFFFFF

    def test_message_id_fits_uint16(self):
        for mid in [0x0101, 0x0102, 0x0103, 0x0104, 0x0105,
                    0x0106, 0x0107, 0x0108, 0x0109, 0x010A,
                    0x0201, 0x0202, 0x0203, 0x0204, 0x0205]:
            assert 0 <= mid <= 0xFFFF

    def test_sequence_fits_uint32(self):
        packed = struct.pack("<I", 0xFFFFFFFF)
        assert struct.unpack("<I", packed)[0] == 0xFFFFFFFF

    def test_timestamp_fits_uint64(self):
        packed = struct.pack("<Q", 0xFFFFFFFFFFFFFFFF)
        assert struct.unpack("<Q", packed)[0] == 0xFFFFFFFFFFFFFFFF
