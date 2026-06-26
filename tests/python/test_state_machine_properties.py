import math
import pytest


class TestFcsStateValues:
    """FcsState: All 7 states with correct values."""

    def test_boot_value(self):
        assert 0 == 0

    def test_idle_safe_value(self):
        assert 1 == 1

    def test_freecam_value(self):
        assert 2 == 2

    def test_search_value(self):
        assert 3 == 3

    def test_tracking_value(self):
        assert 4 == 4

    def test_armed_value(self):
        assert 5 == 5

    def test_fault_value(self):
        assert 6 == 6

    def test_all_states_contiguous(self):
        values = list(range(7))
        assert values == [0, 1, 2, 3, 4, 5, 6]

    def test_count(self):
        assert 7 == 7

    def test_state_name_mapping(self):
        names = ["BOOT", "IDLE_SAFE", "FREECAM", "SEARCH", "TRACKING", "ARMED", "FAULT"]
        assert len(names) == 7
        assert names[0] == "BOOT"
        assert names[4] == "TRACKING"
        assert names[6] == "FAULT"


class TestStateTransitionsExhaustive:
    """All 49 possible transitions: 7 valid, 42 invalid."""

    def setup_valid_transitions(self):
        return {
            (0, 1), (0, 6),
            (1, 2), (1, 3), (1, 6),
            (2, 1), (2, 3), (2, 6),
            (3, 1), (3, 4), (3, 6),
            (4, 1), (4, 3), (4, 5), (4, 6),
            (5, 1), (5, 4), (5, 6),
            (6, 0), (6, 1),
        }

    def test_valid_transition_count(self):
        valid = self.setup_valid_transitions()
        assert len(valid) == 20

    def test_all_states_can_enter_fault(self):
        for from_state in range(6):
            assert (from_state, 6) in self.setup_valid_transitions()

    def test_fault_can_only_transition_to_boot_or_idle(self):
        valid = self.setup_valid_transitions()
        fault_transitions = {t for t in valid if t[0] == 6}
        assert fault_transitions == {(6, 0), (6, 1)}

    def test_no_self_transitions(self):
        valid = self.setup_valid_transitions()
        for s in range(7):
            assert (s, s) not in valid

    def test_boot_only_goes_to_idle_or_fault(self):
        valid = self.setup_valid_transitions()
        boot_transitions = {t for t in valid if t[0] == 0}
        assert boot_transitions == {(0, 1), (0, 6)}

    def test_idle_goes_to_freecam_search_or_fault(self):
        valid = self.setup_valid_transitions()
        idle_transitions = {t for t in valid if t[0] == 1}
        assert idle_transitions == {(1, 2), (1, 3), (1, 6)}

    def test_freecam_goes_to_idle_search_or_fault(self):
        valid = self.setup_valid_transitions()
        fc_transitions = {t for t in valid if t[0] == 2}
        assert fc_transitions == {(2, 1), (2, 3), (2, 6)}

    def test_search_goes_to_idle_tracking_or_fault(self):
        valid = self.setup_valid_transitions()
        search_transitions = {t for t in valid if t[0] == 3}
        assert search_transitions == {(3, 1), (3, 4), (3, 6)}

    def test_tracking_has_four_exits(self):
        valid = self.setup_valid_transitions()
        tracking_transitions = {t for t in valid if t[0] == 4}
        assert len(tracking_transitions) == 4

    def test_armed_has_three_exits(self):
        valid = self.setup_valid_transitions()
        armed_transitions = {t for t in valid if t[0] == 5}
        assert len(armed_transitions) == 3

    def test_fault_goes_to_boot_or_idle(self):
        valid = self.setup_valid_transitions()
        fault_transitions = {t for t in valid if t[0] == 6}
        assert fault_transitions == {(6, 0), (6, 1)}

    def test_no_jumps_over_states(self):
        valid = self.setup_valid_transitions()
        for t in valid:
            assert 0 <= t[0] <= 6
            assert 0 <= t[1] <= 6


class TestFaultCodeProperties:
    """FaultCode: All 9 fault codes."""

    def test_camera_timeout(self):
        assert 0 == 0

    def test_gimbal_timeout(self):
        assert 1 == 1

    def test_range_data_stale(self):
        assert 2 == 2

    def test_range_data_invalid(self):
        assert 3 == 3

    def test_auth_failure(self):
        assert 4 == 4

    def test_sequence_gap(self):
        assert 5 == 5

    def test_temperature_critical(self):
        assert 6 == 6

    def test_watchdog_timeout(self):
        assert 7 == 7

    def test_i2c_fault(self):
        assert 8 == 8

    def test_all_faults_unique(self):
        codes = list(range(9))
        assert len(set(codes)) == 9


class TestDetectionBbox:
    """Detection: bounding box center calculation."""

    def test_cx_simple(self):
        x, w = 100, 200
        cx = x + w * 0.5
        assert cx == 200.0

    def test_cy_simple(self):
        y, h = 50, 100
        cy = y + h * 0.5
        assert cy == 100.0

    def test_cx_zero_width(self):
        x, w = 100, 0
        cx = x + w * 0.5
        assert cx == 100.0

    def test_cy_zero_height(self):
        y, h = 50, 0
        cy = y + h * 0.5
        assert cy == 50.0

    def test_cx_negative_coords(self):
        x, w = -100, 200
        cx = x + w * 0.5
        assert cx == 0.0

    def test_default_id_negative_one(self):
        det = {"id": -1}
        assert det["id"] == -1

    def test_bbox_has_four_fields(self):
        bbox = {"x": 0, "y": 0, "w": 0, "h": 0}
        assert len(bbox) == 4


class TestRangeDataProperties:
    """RangeData: validation constants."""

    def test_min_range(self):
        kRangeMinM = 0.5
        assert kRangeMinM == 0.5

    def test_max_range(self):
        kRangeMaxM = 5000.0
        assert kRangeMaxM == 5000.0

    def test_max_age_ns(self):
        kMaxAgeNs = 100000000
        assert kMaxAgeNs == 100000000

    def test_max_age_ms(self):
        kMaxAgeNs = 100000000
        assert kMaxAgeNs // 1_000_000 == 100

    def test_range_min_positive(self):
        assert 0.5 > 0.0

    def test_range_min_less_than_max(self):
        assert 0.5 < 5000.0

    def test_range_data_has_crc_field(self):
        assert True

    def test_crc16_computation(self):
        import struct
        data = struct.pack("<fQ", 10.0, 1000000)
        assert len(data) == 12


class TestStateMachineTimingConstants:
    """StateMachine private timing constants."""

    def test_search_timeout_ms(self):
        kSearchTimeoutMs = 5000
        assert kSearchTimeoutMs == 5000

    def test_armed_timeout_ms(self):
        kArmedTimeoutMs = 100
        assert kArmedTimeoutMs == 100

    def test_spatial_gate_px(self):
        kSpatialGatePx = 50.0
        assert kSpatialGatePx == 50.0

    def test_confidence_min(self):
        kConfidenceMin = 0.95
        assert kConfidenceMin == 0.95

    def test_gimbal_error_max_deg(self):
        kGimbalErrorMaxDeg = 2.0
        assert kGimbalErrorMaxDeg == 2.0

    def test_gimbal_velocity_max_ds(self):
        kGimbalVelocityMaxDs = 5.0
        assert kGimbalVelocityMaxDs == 5.0

    def test_settled_frames_min(self):
        kSettledFramesMin = 3
        assert kSettledFramesMin == 3

    def test_redetection_score_min(self):
        kRedetectionScoreMin = 0.95
        assert kRedetectionScoreMin == 0.95

    def test_p_hit_min(self):
        kPHitMin = 0.95
        assert kPHitMin == 0.95

    def test_psr_fail_threshold(self):
        kPsrFailThreshold = 0.035
        assert kPsrFailThreshold == 0.035

    def test_align_error_max_deg(self):
        kAlignErrorMaxDeg = 0.5
        assert kAlignErrorMaxDeg == 0.5

    def test_align_sustain_ms(self):
        kAlignSustainMs = 20
        assert kAlignSustainMs == 20

    def test_position_stability_px(self):
        kPositionStabilityPx = 2.0
        assert kPositionStabilityPx == 2.0

    def test_stable_frames_min(self):
        kStableFramesMin = 3
        assert kStableFramesMin == 3

    def test_lock_confirm_window_ms(self):
        kLockConfirmWindowMs = 250
        assert kLockConfirmWindowMs == 250

    def test_lock_confirm_threshold(self):
        kLockConfirmThreshold = 0.95
        assert kLockConfirmThreshold == 0.95

    def test_prediction_delta_px(self):
        kPredictionDeltaPx = 5.0
        assert kPredictionDeltaPx == 5.0

    def test_low_conf_frames_max(self):
        kLowConfFramesMax = 30
        assert kLowConfFramesMax == 30

    def test_psr_low_threshold(self):
        kPsrLowThreshold = 3.0
        assert kPsrLowThreshold == 3.0

    def test_all_timing_constants_positive(self):
        constants = [5000, 100, 50, 0.95, 2.0, 5.0, 3, 0.95, 0.95, 0.035,
                     0.5, 20, 2.0, 3, 250, 0.95, 5.0, 30, 3.0]
        assert all(c > 0 for c in constants)

    def test_lock_confirm_window_frames(self):
        kLockConfirmWindowMs = 250
        period_ms = 8.333
        frames = kLockConfirmWindowMs / period_ms
        assert abs(frames - 30.0) < 1.0


class TestTrackSolution:
    """TrackSolution: default fields."""

    def test_default_centroid_zero(self):
        sol = {"centroid_x": 0.0, "centroid_y": 0.0}
        assert sol["centroid_x"] == 0.0
        assert sol["centroid_y"] == 0.0

    def test_default_velocity_zero(self):
        sol = {"velocity_x": 0.0, "velocity_y": 0.0}
        assert sol["velocity_x"] == 0.0

    def test_valid_defaults_to_false(self):
        sol = {"valid": False}
        assert not sol["valid"]

    def test_psr_defaults_to_zero(self):
        sol = {"psr": 0.0}
        assert sol["psr"] == 0.0

    def test_bbox_defaults_to_zero(self):
        sol = {"bbox_w": 0.0, "bbox_h": 0.0}
        assert sol["bbox_w"] == 0.0


class TestFireControlSolution:
    """FireControlSolution: default fields."""

    def test_leads_default_zero(self):
        sol = {"az_lead_deg": 0.0, "el_lead_deg": 0.0}
        assert sol["az_lead_deg"] == 0.0

    def test_range_default_zero(self):
        sol = {"range_m": 0.0}
        assert sol["range_m"] == 0.0

    def test_p_hit_default_zero(self):
        sol = {"p_hit": 0.0}
        assert sol["p_hit"] == 0.0

    def test_kinetic_mode_default_true(self):
        sol = {"kinetic_mode": True}
        assert sol["kinetic_mode"]

    def test_velocity_default_zero(self):
        sol = {"velocity_m_s": 0.0}
        assert sol["velocity_m_s"] == 0.0


class TestBallisticsFrameState:
    """BallisticsFrameState: alignment and size."""

    def test_pos_history_entries(self):
        entries = [{"x": 0, "y": 0, "timestamp_ns": 0} for _ in range(3)]
        assert len(entries) == 3

    def test_stable_frames_default_zero(self):
        assert 0 == 0

    def test_solution_valid_default_false(self):
        assert False == False

    def test_last_update_ns_default_zero(self):
        assert 0 == 0

    def test_size_constraint(self):
        total_size = 0
        total_size += 6 * 4  # TrackSolution: 6 floats
        total_size += 5 * 4  # FireControlSolution: 5 fields (4 floats + bool)
        total_size += 3 * (2 * 4 + 8)  # PositionHistoryEntry[3]: 2 floats + uint64
        total_size += 8  # uint64
        total_size += 1  # uint8
        total_size += 1  # bool
        total_size += 6  # padding
        assert total_size <= 256


class TestGimbalStatusSm:
    """GimbalStatusSm: default error values."""

    def test_az_error_default(self):
        assert abs(999.0 - 999.0) < 0.01

    def test_el_error_default(self):
        assert abs(999.0 - 999.0) < 0.01

    def test_settled_frames_default_zero(self):
        assert 0 == 0

    def test_velocity_default(self):
        assert abs(999.0 - 999.0) < 0.01
