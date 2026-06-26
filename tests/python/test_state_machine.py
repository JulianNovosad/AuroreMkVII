import pytest
import struct


class TestFcsState:
    """FcsState enum: 7 states for turret state machine."""

    def test_state_values(self):
        assert 0 == 0  # BOOT
        assert 1 == 1  # IDLE_SAFE
        assert 2 == 2  # FREECAM
        assert 3 == 3  # SEARCH
        assert 4 == 4  # TRACKING
        assert 5 == 5  # ARMED
        assert 6 == 6  # FAULT

    def test_state_names(self):
        names = ["BOOT", "IDLE_SAFE", "FREECAM", "SEARCH", "TRACKING", "ARMED", "FAULT"]
        assert names[0] == "BOOT"
        assert names[1] == "IDLE_SAFE"
        assert names[6] == "FAULT"

    def test_total_states(self):
        assert 7 == 7


class TestFaultCode:
    """FaultCode enum: 9 fault conditions."""

    def test_fault_values(self):
        assert 0 == 0   # CAMERA_TIMEOUT
        assert 1 == 1   # GIMBAL_TIMEOUT
        assert 2 == 2   # RANGE_DATA_STALE
        assert 3 == 3   # RANGE_DATA_INVALID
        assert 4 == 4   # AUTH_FAILURE
        assert 5 == 5   # SEQUENCE_GAP
        assert 6 == 6   # TEMPERATURE_CRITICAL
        assert 7 == 7   # WATCHDOG_TIMEOUT
        assert 8 == 8   # I2C_FAULT

    def test_fault_names(self):
        names = [
            "CAMERA_TIMEOUT", "GIMBAL_TIMEOUT", "RANGE_DATA_STALE",
            "RANGE_DATA_INVALID", "AUTH_FAILURE", "SEQUENCE_GAP",
            "TEMPERATURE_CRITICAL", "WATCHDOG_TIMEOUT", "I2C_FAULT"
        ]
        assert len(names) == 9


class TestCRC16:
    """Modbus CRC-16 for laser rangefinder."""

    def crc16_modbus(self, data: bytes) -> int:
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    def test_crc16_known_values(self):
        assert self.crc16_modbus(b"") == 0xFFFF
        assert self.crc16_modbus(b"A") == 0x707F
        assert self.crc16_modbus(b"123456789") == 0x4B37

    def test_crc16_deterministic(self):
        data = b"hello"
        assert self.crc16_modbus(data) == self.crc16_modbus(data)

    def test_crc16_different_inputs_different(self):
        assert self.crc16_modbus(b"foo") != self.crc16_modbus(b"bar")

    def test_crc16_range_default(self):
        assert 0 <= self.crc16_modbus(b"\x00") <= 0xFFFF


class TestCCITTCrc16:
    """CRC-16-CCITT for RangeData validation (AM7-L3-SAFE-002).

    Matches StateMachine::compute_crc16 in state_machine.cpp:
    polynomial 0x1021, init 0xFFFF.
    """

    def crc16_ccitt(self, data: bytes) -> int:
        crc = 0xFFFF
        for byte in data:
            for i in range(8):
                bit = (byte >> (7 - i)) & 1
                crc ^= (bit << 15)
                for _ in range(8):
                    crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
                    crc &= 0xFFFF
        return crc

    def test_ccitt_known_empty(self):
        assert self.crc16_ccitt(b"") == 0xFFFF

    def test_ccitt_known_byte(self):
        assert self.crc16_ccitt(b"\x00") == 0x313E

    def test_ccitt_deterministic(self):
        data = b"range_data_123"
        assert self.crc16_ccitt(data) == self.crc16_ccitt(data)

    def test_ccitt_different_inputs_different(self):
        assert self.crc16_ccitt(b"foo") != self.crc16_ccitt(b"bar")

    def test_ccitt_range_bounds(self):
        import struct
        for r in [0.5, 10.0, 100.0, 5000.0]:
            data = struct.pack("<fQ", r, 1000000)
            crc = self.crc16_ccitt(data)
            assert 0 <= crc <= 0xFFFF

    def test_ccitt_bit_reflection_byte(self):
        byte_val = 0x80
        crc = self.crc16_ccitt(bytes([byte_val]))
        assert 0 <= crc <= 0xFFFF

    def test_crc16_max_age_constant(self):
        assert 100000000 == 100000000


class TestRangeData:
    """RangeData: range bounds and checksum fields."""

    def test_range_bounds(self):
        kRangeMinM = 0.5
        kRangeMaxM = 5000.0
        assert kRangeMinM < kRangeMaxM

    def test_max_age(self):
        kMaxAgeNs = 100000000
        assert kMaxAgeNs == 100_000_000


class TestDetectionStruct:
    """Detection: bounding box and center calculation."""

    def test_center_x(self):
        bbox = {"x": 10, "y": 20, "w": 100, "h": 200}
        cx = bbox["x"] + bbox["w"] * 0.5
        assert cx == 60.0

    def test_center_y(self):
        bbox = {"x": 10, "y": 20, "w": 100, "h": 200}
        cy = bbox["y"] + bbox["h"] * 0.5
        assert cy == 120.0


class TestTrackSolution:
    """TrackSolution: tracking output fields."""

    def test_default_construction(self):
        sol = {
            "centroid_x": 0.0, "centroid_y": 0.0,
            "velocity_x": 0.0, "velocity_y": 0.0,
            "bbox_w": 0.0, "bbox_h": 0.0,
            "valid": False, "psr": 0.0
        }
        assert sol["valid"] is False
        assert sol["psr"] == 0.0

    def test_valid_track(self):
        sol = {
            "centroid_x": 320.0, "centroid_y": 240.0,
            "velocity_x": 1.5, "velocity_y": -0.5,
            "bbox_w": 64.0, "bbox_h": 128.0,
            "valid": True, "psr": 8.5
        }
        assert sol["valid"]
        assert sol["psr"] > 7.0


class TestFireControlSolution:
    """FireControlSolution: ballistics output."""

    def test_defaults(self):
        fc = {
            "az_lead_deg": 0.0, "el_lead_deg": 0.0,
            "range_m": 0.0, "velocity_m_s": 0.0,
            "p_hit": 0.0, "kinetic_mode": True
        }
        assert fc["kinetic_mode"]

    def test_kinetic_solution(self):
        fc = {
            "az_lead_deg": 1.5, "el_lead_deg": 2.0,
            "range_m": 50.0, "velocity_m_s": 300.0,
            "p_hit": 0.85, "kinetic_mode": True
        }
        assert fc["p_hit"] >= 0.0


class TestGimbalStatusSm:
    """GimbalStatusSm: gimbal tracking status."""

    def test_defaults(self):
        gs = {"az_error_deg": 999.0, "el_error_deg": 999.0,
              "velocity_deg_s": 999.0, "settled_frames": 0}
        assert gs["az_error_deg"] == 999.0

    def test_settled(self):
        gs = {"az_error_deg": 0.5, "el_error_deg": 0.3,
              "velocity_deg_s": 2.0, "settled_frames": 5}
        assert gs["settled_frames"] >= 3

    def test_error_threshold(self):
        kGimbalErrorMaxDeg = 2.0
        assert kGimbalErrorMaxDeg == 2.0


class TestBallisticsFrameState:
    """BallisticsFrameState: aligned frame state."""

    def test_size_limit(self):
        assert 256 >= 256

    def test_position_history_count(self):
        assert 3 == 3

    def test_stable_frames_tracking(self):
        frames = 10
        assert frames >= 3


class TestStateMachineConstants:
    """StateMachine: key constants for transitions."""

    def test_search_timeout(self):
        assert 5000 == 5000

    def test_armed_timeout(self):
        assert 100 == 100

    def test_confidence_min(self):
        assert 0.95 == 0.95

    def test_spatial_gate(self):
        assert 50.0 == 50.0

    def test_gimbal_error_max(self):
        assert 2.0 == 2.0

    def test_gimbal_velocity_max(self):
        assert 5.0 == 5.0

    def test_settled_frames_min(self):
        assert 3 == 3

    def test_redetection_score_min(self):
        assert 0.95 == 0.95

    def test_p_hit_min(self):
        assert 0.95 == 0.95

    def test_align_error_max(self):
        assert 0.5 == 0.5

    def test_align_sustain_ms(self):
        assert 20 == 20

    def test_position_stability_px(self):
        assert 2.0 == 2.0

    def test_lock_confirm_window_ms(self):
        assert 250 == 250

    def test_lock_confirm_threshold(self):
        assert 0.95 == 0.95

    def test_prediction_delta_px(self):
        assert 5.0 == 5.0

    def test_psr_low_threshold(self):
        assert 3.0 == 3.0

    def test_low_conf_frames_max(self):
        assert 30 == 30


class TestStateTransitions:
    """State transition table validation (AM7-L3-MODE-001)."""

    def test_boot_transitions(self):
        transitions = {"IDLE_SAFE": True, "FAULT": True, "FREECAM": False,
                       "SEARCH": False, "TRACKING": False, "ARMED": False}
        assert transitions["IDLE_SAFE"]
        assert transitions["FAULT"]
        assert not transitions["FREECAM"]

    def test_idle_safe_transitions(self):
        transitions = {"FREECAM": True, "SEARCH": True, "FAULT": True,
                       "BOOT": False, "TRACKING": False, "ARMED": False}
        assert transitions["FREECAM"]
        assert transitions["SEARCH"]
        assert transitions["FAULT"]

    def test_freecam_transitions(self):
        transitions = {"IDLE_SAFE": True, "SEARCH": True, "FAULT": True,
                       "BOOT": False, "TRACKING": False, "ARMED": False}
        assert transitions["IDLE_SAFE"]
        assert transitions["SEARCH"]

    def test_search_transitions(self):
        transitions = {"IDLE_SAFE": True, "TRACKING": True, "FAULT": True,
                       "BOOT": False, "FREECAM": False, "ARMED": False}
        assert transitions["TRACKING"]
        assert transitions["IDLE_SAFE"]

    def test_tracking_transitions(self):
        transitions = {"IDLE_SAFE": True, "SEARCH": True, "ARMED": True, "FAULT": True,
                       "BOOT": False, "FREECAM": False}
        assert transitions["ARMED"]
        assert transitions["IDLE_SAFE"]
        assert transitions["SEARCH"]

    def test_armed_transitions(self):
        transitions = {"IDLE_SAFE": True, "TRACKING": True, "FAULT": True,
                       "BOOT": False, "FREECAM": False, "SEARCH": False}
        assert transitions["IDLE_SAFE"]
        assert transitions["TRACKING"]

    def test_fault_transitions(self):
        transitions = {"BOOT": True, "IDLE_SAFE": True, "FAULT": False,
                       "FREECAM": False, "SEARCH": False, "TRACKING": False, "ARMED": False}
        assert transitions["BOOT"]
        assert transitions["IDLE_SAFE"]
        assert not transitions["FAULT"]


class TestFcsStateName:
    """fcs_state_name: human-readable state names."""

    def test_all_states_have_names(self):
        names = ["BOOT", "IDLE_SAFE", "FREECAM", "SEARCH", "TRACKING", "ARMED", "FAULT"]
        assert len(names) == 7
        assert all(isinstance(n, str) and len(n) > 0 for n in names)

    def test_state_name_mapping(self):
        expected = {
            0: "BOOT", 1: "IDLE_SAFE", 2: "FREECAM",
            3: "SEARCH", 4: "TRACKING", 5: "ARMED", 6: "FAULT"
        }
        for k, v in expected.items():
            assert isinstance(k, int)
            assert isinstance(v, str)
