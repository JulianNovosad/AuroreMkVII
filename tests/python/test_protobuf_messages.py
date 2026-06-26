from aurore_pb2 import (
    Telemetry, TrackState, BallisticSolution, GimbalStatus, SystemHealth,
    Command, ModeSwitch, FreecamTarget, ArmCommand, ConfigPatch,
    ProtoFcsState, OperatingMode,
)
import pytest


class TestTrackState:
    def test_defaults_are_zero(self):
        t = TrackState()
        assert t.valid is False
        assert t.centroid_x == 0.0
        assert t.centroid_y == 0.0
        assert t.velocity_x == 0.0
        assert t.velocity_y == 0.0
        assert t.confidence == 0.0
        assert t.range_m == 0.0

    def test_set_and_serialize(self):
        t = TrackState()
        t.valid = True
        t.centroid_x = 768.5
        t.centroid_y = 432.2
        t.velocity_x = 2.5
        t.velocity_y = -1.3
        t.confidence = 0.92
        t.range_m = 150.0

        data = t.SerializeToString()
        t2 = TrackState()
        t2.ParseFromString(data)

        assert t2.valid is True
        assert abs(t2.centroid_x - 768.5) < 0.01
        assert abs(t2.centroid_y - 432.2) < 0.01
        assert abs(t2.velocity_x - 2.5) < 0.01
        assert abs(t2.velocity_y - (-1.3)) < 0.01
        assert abs(t2.confidence - 0.92) < 0.01
        assert abs(t2.range_m - 150.0) < 0.01

    def test_confidence_range(self):
        t = TrackState()
        t.valid = True
        t.confidence = 0.0
        data = t.SerializeToString()
        t2 = TrackState()
        t2.ParseFromString(data)
        assert t2.confidence == 0.0

        t.confidence = 1.0
        data = t.SerializeToString()
        t2.ParseFromString(data)
        assert t2.confidence == 1.0


class TestBallisticSolution:
    def test_defaults(self):
        b = BallisticSolution()
        assert b.valid is False
        assert b.p_hit == 0.0

    def test_round_trip(self):
        b = BallisticSolution()
        b.valid = True
        b.az_lead_deg = 1.5
        b.el_lead_deg = 2.3
        b.range_m = 200.0
        b.p_hit = 0.85

        data = b.SerializeToString()
        b2 = BallisticSolution()
        b2.ParseFromString(data)
        assert b2.valid is True
        assert abs(b2.az_lead_deg - 1.5) < 0.01
        assert abs(b2.p_hit - 0.85) < 0.01


class TestGimbalStatus:
    def test_defaults(self):
        g = GimbalStatus()
        assert g.az_deg == 0.0
        assert g.el_deg == 0.0
        assert g.settled is False

    def test_round_trip(self):
        g = GimbalStatus()
        g.az_deg = 45.0
        g.el_deg = 30.0
        g.az_error_deg = 0.1
        g.el_error_deg = 0.05
        g.settled = True

        data = g.SerializeToString()
        g2 = GimbalStatus()
        g2.ParseFromString(data)
        assert abs(g2.az_deg - 45.0) < 0.01
        assert abs(g2.el_deg - 30.0) < 0.01
        assert g2.settled is True


class TestSystemHealth:
    def test_defaults(self):
        h = SystemHealth()
        assert h.cpu_temp_c == 0.0
        assert h.frame_count == 0
        assert h.deadline_misses == 0
        assert h.emergency_active is False

    def test_fcs_state_enum_values(self):
        h = SystemHealth()
        assert ProtoFcsState.PROTO_BOOT == 0
        assert ProtoFcsState.PROTO_IDLE_SAFE == 1
        assert ProtoFcsState.PROTO_FREECAM == 2
        assert ProtoFcsState.PROTO_SEARCH == 3
        assert ProtoFcsState.PROTO_TRACKING == 4
        assert ProtoFcsState.PROTO_ARMED == 5
        assert ProtoFcsState.PROTO_FAULT == 6

    def test_operating_mode_enum_values(self):
        assert OperatingMode.AUTO == 0
        assert OperatingMode.FREECAM_MODE == 1

    def test_round_trip(self):
        h = SystemHealth()
        h.cpu_temp_c = 55.2
        h.cpu_usage_pct = 42.5
        h.frame_count = 12345
        h.deadline_misses = 0
        h.emergency_active = False
        h.fcs_state = ProtoFcsState.PROTO_TRACKING
        h.mode = OperatingMode.AUTO

        data = h.SerializeToString()
        h2 = SystemHealth()
        h2.ParseFromString(data)
        assert abs(h2.cpu_temp_c - 55.2) < 0.01
        assert h2.fcs_state == ProtoFcsState.PROTO_TRACKING
        assert h2.mode == OperatingMode.AUTO


class TestTelemetry:
    def test_empty_telemetry_size(self):
        t = Telemetry()
        data = t.SerializeToString()
        assert data is not None

    def test_full_telemetry_round_trip(self):
        t = Telemetry()
        t.timestamp_ns = 1234567890000000000

        t.track.valid = True
        t.track.centroid_x = 768.0
        t.track.centroid_y = 432.0
        t.track.confidence = 0.95

        t.ballistic.valid = True
        t.ballistic.p_hit = 0.88

        t.gimbal.az_deg = 10.0
        t.gimbal.el_deg = 20.0

        t.health.cpu_temp_c = 60.0
        t.health.fcs_state = ProtoFcsState.PROTO_TRACKING
        t.health.mode = OperatingMode.AUTO

        data = t.SerializeToString()
        t2 = Telemetry()
        t2.ParseFromString(data)

        assert t2.timestamp_ns == 1234567890000000000
        assert t2.track.valid is True
        assert abs(t2.track.centroid_x - 768.0) < 0.01
        assert t2.health.fcs_state == ProtoFcsState.PROTO_TRACKING

    def test_telemetry_not_trivially_zero_length(self):
        t = Telemetry()
        t.timestamp_ns = 1
        data = t.SerializeToString()
        assert len(data) > 0


class TestCommandMessages:
    def test_mode_switch_auto(self):
        c = Command()
        c.mode_switch.mode = OperatingMode.AUTO
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.WhichOneof("payload") == "mode_switch"
        assert c2.mode_switch.mode == OperatingMode.AUTO

    def test_mode_switch_freecam(self):
        c = Command()
        c.mode_switch.mode = OperatingMode.FREECAM_MODE
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.mode_switch.mode == OperatingMode.FREECAM_MODE

    def test_freecam_command(self):
        c = Command()
        c.freecam.az_deg = 45.0
        c.freecam.el_deg = 30.0
        c.freecam.velocity_dps = 10.0
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.WhichOneof("payload") == "freecam"
        assert abs(c2.freecam.az_deg - 45.0) < 0.01

    def test_arm_command(self):
        c = Command()
        c.arm.authorized = True
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.WhichOneof("payload") == "arm"
        assert c2.arm.authorized is True

    def test_config_patch(self):
        c = Command()
        c.config.values["param"] = "value"
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.WhichOneof("payload") == "config"
        assert c2.config.values["param"] == "value"

    def test_payload_oneof_works(self):
        c = Command()
        c.mode_switch.mode = OperatingMode.AUTO
        assert c.WhichOneof("payload") == "mode_switch"
        data = c.SerializeToString()
        c2 = Command()
        c2.ParseFromString(data)
        assert c2.WhichOneof("payload") == "mode_switch"
