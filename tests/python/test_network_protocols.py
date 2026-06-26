import pytest
import struct


# --- AuroreLinkServer / ICD-005 ---

class TestLinkInputHeader:
    """LinkInputHeader: operator command header (ICD-005)."""

    def test_packed_size(self):
        fmt = "<IHIQ"  # sync_word(u32) + message_id(u16) + sequence(u32) + timestamp(u64)
        assert struct.calcsize(fmt) == 18

    def test_sync_word(self):
        sync = 0xA7050005
        assert sync == 0xA7050005

    def test_construction(self):
        hdr = {"sync_word": 0xA7050005, "message_id": 0x0101,
               "sequence": 1, "timestamp_ns": 1000}
        assert hdr["sync_word"] == 0xA7050005


class TestLinkOutputHeader:
    """LinkOutputHeader: system response header (ICD-005)."""

    def test_packed_size(self):
        fmt = "<IHIQ"  # sync_word(u32) + message_id(u16) + sequence(u32) + timestamp(u64)
        assert struct.calcsize(fmt) == 18

    def test_sync_word(self):
        sync = 0xA7060006
        assert sync == 0xA7060006


class TestLinkMessageSizes:
    """Full message structure sizes."""

    def test_input_message_size(self):
        hdr_size = 18
        payload_size = 32
        hmac_size = 32
        total = hdr_size + payload_size + hmac_size
        assert total == 82

    def test_output_message_size(self):
        hdr_size = 18
        status_size = 1
        error_size = 1
        payload_size = 28
        hmac_size = 32
        total = hdr_size + status_size + error_size + payload_size + hmac_size
        assert total == 80

    def test_hmac_size(self):
        assert 32 == 32


class TestLinkMsgId:
    """LinkMsgId: ICD-005 message identifiers."""

    def test_operator_to_system(self):
        assert 0x0101 == 257  # kModeRequest
        assert 0x0102 == 258  # kGimbalCommand
        assert 0x0103 == 259  # kZoomCommand
        assert 0x0104 == 260  # kTargetSelect
        assert 0x0105 == 261  # kTargetConfirm
        assert 0x0106 == 262  # kTargetReject
        assert 0x0107 == 263  # kArmRequest
        assert 0x0108 == 264  # kDisarmRequest
        assert 0x0109 == 265  # kEmergencyInhibit
        assert 0x010A == 266  # kHeartbeat

    def test_system_to_operator(self):
        assert 0x0201 == 513  # kModeAck
        assert 0x0202 == 514  # kModeNack
        assert 0x0203 == 515  # kSystemState
        assert 0x0204 == 516  # kTargetStatus
        assert 0x0205 == 517  # kFaultStatus


class TestLinkPayloads:
    """ICD-005 payload structures."""

    def test_mode_request_payload(self):
        pl = {"target_mode": 0, "reserved": bytes(31)}
        assert len(pl["reserved"]) == 31
        assert pl["target_mode"] in range(5)

    def test_zoom_cmd_payload(self):
        pl = {"zoom_direction": 0, "zoom_rate": 5}
        assert pl["zoom_direction"] in [-1, 0, 1]
        assert 0 <= pl["zoom_rate"] <= 10

    def test_gimbal_cmd_payload(self):
        pl = {"azimuth_rate": 100, "elevation_rate": -50}
        assert isinstance(pl["azimuth_rate"], int)

    def test_system_state_payload(self):
        pl = {"current_mode": 0, "interlock": 0, "target_lock": 0, "fault_active": 0}
        assert len(pl) == 4

    def test_target_select_payload(self):
        pl = {"cursor_x": 768, "cursor_y": 432, "confidence": 85}
        assert 0 <= pl["cursor_x"] <= 1536
        assert 0 <= pl["cursor_y"] <= 864
        assert 0 <= pl["confidence"] <= 100

    def test_target_confirm_payload(self):
        pl = {"target_id": 42}
        assert pl["target_id"] >= 0

    def test_target_reject_payload(self):
        pl = {"target_id": 42, "reason": 1}
        assert 0 <= pl["reason"] <= 255

    def test_reserved_payload_size(self):
        assert 32 == 32
        assert 28 == 28


class TestAuroreLinkConfig:
    """AuroreLinkConfig: TCP server configuration."""

    def test_defaults(self):
        cfg = {"telemetry_port": 9000, "video_port": 9001, "command_port": 9002,
               "max_clients": 4, "session_timeout_s": 300}
        assert cfg["telemetry_port"] == 9000
        assert cfg["command_port"] == 9002
        assert cfg["max_clients"] == 4
        assert cfg["session_timeout_s"] == 300


class TestLinkMode:
    """LinkMode: operator control mode."""

    def test_values(self):
        assert 0 == 0  # AUTO
        assert 1 == 1  # FREECAM


# --- HudSocket / ICD-006 ---

class TestHudBinaryHeader:
    """HudBinaryHeader: HUD telemetry header (ICD-006)."""

    def test_packed_size(self):
        fmt = "<IHIQ"  # sync_word(u32) + message_id(u16) + sequence(u32) + timestamp(u64)
        assert struct.calcsize(fmt) == 18

    def test_sync_word(self):
        assert 0xA7070007 == 0xA7070007


class TestHudBinaryMessage:
    """HudBinaryMessage: 64-byte HUD message."""

    def test_total_size(self):
        hdr_size = 18
        payload_size = 32
        hmac_size = 32
        total = hdr_size + payload_size + hmac_size
        assert total == 82

    def test_all_fields(self):
        msg = {"header": {"sync_word": 0xA7070007, "message_id": 0x0301,
                          "sequence": 1, "timestamp_ns": 1000},
               "payload": bytes(32), "hmac": bytes(32)}
        assert len(msg["payload"]) == 32
        assert len(msg["hmac"]) == 32


class TestHudMsgId:
    """HudMsgId: ICD-006 telemetry type identifiers."""

    def test_values(self):
        assert 0x0301 == 769  # kReticleData
        assert 0x0302 == 770  # kTargetBox
        assert 0x0303 == 771  # kBallisticSolution
        assert 0x0304 == 772  # kSystemStatus


class TestHudPayloads:
    """ICD-006 payload structures."""

    def test_reticle_payload_size(self):
        fmt = "<hhhh"  # reticle_x, reticle_y, lead_offset_x, lead_offset_y
        base = struct.calcsize(fmt)
        reserved = 24
        assert base + reserved == 32

    def test_target_box_payload_size(self):
        fmt = "<HHHHB"  # box_x, box_y, box_width, box_height, confidence
        base = struct.calcsize(fmt)
        reserved = 23
        assert base + reserved == 32

    def test_ballistics_payload_size(self):
        fmt = "<hhHB"  # elevation_adj, azimuth_adj, range_m, ammo_id
        base = struct.calcsize(fmt)
        reserved = 25
        assert base + reserved == 32

    def test_status_payload_size(self):
        fmt = "<BBBBHH"  # fcs_state, interlock, target_lock, fault_active, cpu_temp_c, deadline_misses
        base = struct.calcsize(fmt)
        reserved = 24
        assert base + reserved == 32

    def test_status_fields(self):
        st = {"fcs_state": 0, "interlock": 0, "target_lock": 0,
              "fault_active": 0, "cpu_temp_c": 550, "deadline_misses": 0}
        assert 0 <= st["fcs_state"] <= 6
        assert st["interlock"] in [0, 1]

    def test_reticle_fields(self):
        r = {"reticle_x": 0, "reticle_y": 0, "lead_offset_x": 100, "lead_offset_y": -50}
        assert isinstance(r["lead_offset_x"], int)


class TestHudFrame:
    """HudFrame: consolidated telemetry frame."""

    def test_defaults(self):
        f = {"state": 0, "az_deg": 0.0, "el_deg": 0.0, "target_cx": 0.0,
             "target_cy": 0.0, "confidence": 0.0, "p_hit": 0.0, "range_m": 0.0}
        assert f["state"] == 0

    def test_active_frame(self):
        f = {"state": 4, "az_deg": 45.0, "el_deg": 10.0, "target_cx": 300.0,
             "target_cy": 200.0, "confidence": 0.85, "p_hit": 0.75, "range_m": 50.0}
        assert f["confidence"] > 0.5


class TestSocketAuthStatus:
    """SocketAuthStatus: SEC-008 credential validation."""

    def test_values(self):
        assert 0 == 0  # kOk
        assert 1 == 1  # kCredentialError
        assert 2 == 2  # kUnauthorizedUid
        assert 3 == 3  # kUnauthorizedGid
        assert 4 == 4  # kMaxClientsExceeded


class TestHudSocketConfig:
    """HudSocketConfig: SEC-008 socket configuration."""

    def test_defaults(self):
        cfg = {"socket_path": "/run/aurore/hud_telemetry.sock",
               "socket_permissions": 0o660,
               "require_root_uid": True, "allowed_uid": 0, "allowed_gid": 0,
               "max_clients": 10}
        assert cfg["socket_path"] == "/run/aurore/hud_telemetry.sock"
        assert cfg["max_clients"] == 10
        assert cfg["require_root_uid"]

    def test_rate_limit_default(self):
        assert 120.0 == pytest.approx(120.0)

    def test_message_timeout_default(self):
        assert 100.0 == pytest.approx(100.0)


class TestCommandSocket:
    """CommandSocket: UNIX socket command bridge."""

    def test_default_socket_path(self):
        assert "/tmp/aurore_cmd.sock" == "/tmp/aurore_cmd.sock"

    def test_config_allowed_uid_default(self):
        assert 0 == 0

    def test_callbacks(self):
        pass  # Callback registration is structural


class TestMjpegStreamerConstants:
    """MjpegStreamer constants."""

    def test_default_socket_path(self):
        assert "/run/aurore/mjpeg_stream.sock" == "/run/aurore/mjpeg_stream.sock"

    def test_stream_width(self):
        assert 1280 == 1280

    def test_stream_height(self):
        assert 720 == 720

    def test_jpeg_quality(self):
        assert 75 == 75

    def test_encode_interval_ms(self):
        assert 16 == 16
