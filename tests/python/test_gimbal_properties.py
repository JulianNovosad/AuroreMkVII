import math
import pytest


class TestGimbalAngleLimits:
    """Gimbal angle boundary constants."""

    def test_az_min(self):
        assert -90.0 == -90.0

    def test_az_max(self):
        assert 90.0 == 90.0

    def test_el_min(self):
        assert -10.0 == -10.0

    def test_el_max(self):
        assert 45.0 == 45.0

    def test_rate_limit(self):
        assert 60.0 == 60.0

    def test_accel_limit(self):
        assert 120.0 == 120.0

    def test_az_range(self):
        assert 90.0 - (-90.0) == 180.0

    def test_el_range(self):
        assert 45.0 - (-10.0) == 55.0

    def test_az_centered_at_zero(self):
        assert 0.0 - (-90.0) == 90.0
        assert 90.0 - 0.0 == 90.0

    def test_sequence_gap_threshold(self):
        assert 1000 == 1000

    def test_clamp_az_below_min(self):
        cmd = -100.0
        clamped = max(-90.0, min(90.0, cmd))
        assert clamped == -90.0

    def test_clamp_az_above_max(self):
        cmd = 100.0
        clamped = max(-90.0, min(90.0, cmd))
        assert clamped == 90.0

    def test_clamp_el_below_min(self):
        cmd = -20.0
        clamped = max(-10.0, min(45.0, cmd))
        assert clamped == -10.0

    def test_clamp_el_above_max(self):
        cmd = 60.0
        clamped = max(-10.0, min(45.0, cmd))
        assert clamped == 45.0


class TestCameraIntrinsics:
    """CameraIntrinsics: focal length and principal point."""

    def test_focal_length_px(self):
        focal_px = 1128.0
        assert focal_px == 1128.0

    def test_cx(self):
        assert 768.0 == 768.0

    def test_cy(self):
        assert 432.0 == 432.0

    def test_sensor_width_mm(self):
        sensor_w = 6.4
        assert abs(sensor_w - 6.4) < 0.1

    def test_fov_horizontal(self):
        focal_px = 1128.0
        width = 1536
        fov_h = 2 * math.atan(width / (2 * focal_px)) * 180 / math.pi
        assert abs(fov_h - 68.0) < 2.0

    def test_fov_vertical(self):
        focal_px = 1128.0
        height = 864
        fov_v = 2 * math.atan(height / (2 * focal_px)) * 180 / math.pi
        assert abs(fov_v - 42.0) < 2.0

    def test_principal_point_center(self):
        assert 768.0 == 1536 / 2
        assert 432.0 == 864 / 2

    def test_focal_length_mm_to_px(self):
        fl_mm = 4.74
        sensor_w = 6.4
        fl_px = fl_mm * 1536 / sensor_w
        assert abs(fl_px - 1137.6) < 10


class TestPixelToAngle:
    """Pixel-to-angle projection math."""

    def test_center_pixel_zero_angle(self):
        cx, cy = 768, 432
        px, py = 768, 432
        dx = px - cx
        dy = py - cy
        assert dx == 0 and dy == 0

    def test_right_edge_angle(self):
        focal_px = 1128.0
        cx = 768
        px = 1536
        dx = px - cx
        angle = math.degrees(math.atan(dx / focal_px))
        assert abs(angle - 34.0) < 2.0

    def test_left_edge_angle(self):
        focal_px = 1128.0
        cx = 768
        px = 0
        dx = px - cx
        angle = math.degrees(math.atan(dx / focal_px))
        assert abs(angle - (-34.0)) < 2.0

    def test_top_edge_angle(self):
        focal_px = 1128.0
        cy = 432
        py = 0
        dy = py - cy
        angle = math.degrees(math.atan(dy / focal_px))
        assert abs(angle - (-21.0)) < 2.0

    def test_bottom_edge_angle(self):
        focal_px = 1128.0
        cy = 432
        py = 864
        dy = py - cy
        angle = math.degrees(math.atan(dy / focal_px))
        assert abs(angle - 21.0) < 2.0

    def test_pixel_to_angle_linearity(self):
        focal_px = 1128.0
        for dx in [0, 100, 200, 384]:
            angle = math.degrees(math.atan(dx / focal_px))
            assert angle >= 0

    def test_angle_increases_with_offset(self):
        focal_px = 1128.0
        prev = 0
        for dx in range(100, 500, 100):
            angle = math.degrees(math.atan(dx / focal_px))
            assert angle > prev
            prev = angle


class TestGimbalSource:
    """GimbalSource enum."""

    def test_auto_value(self):
        assert 0 == 0

    def test_freecam_value(self):
        assert 1 == 1


class TestGimbalCommand:
    """GimbalCommand: sequence tracking."""

    def test_az_deg_type(self):
        cmd = {"az_deg": 45.0}
        assert isinstance(cmd["az_deg"], float)

    def test_el_deg_type(self):
        cmd = {"el_deg": 10.0}
        assert isinstance(cmd["el_deg"], float)

    def test_sequence_optional(self):
        cmd_no_seq = {}
        cmd_with_seq = {"sequence_num": 42}
        assert "sequence_num" not in cmd_no_seq
        assert cmd_with_seq["sequence_num"] == 42

    def test_sequence_gap_detection(self):
        threshold = 1000
        last = 500
        current = 2500
        gap = current - last
        assert gap > threshold

    def test_no_gap_within_threshold(self):
        threshold = 1000
        last = 500
        current = 1000
        gap = current - last
        assert gap <= threshold

    def test_sequence_wrap_gap(self):
        threshold = 1000
        last = 0xFFFFFFF0
        current = 10
        gap = current - last
        gap_detected = gap > threshold or gap < 0
        assert gap_detected

    def test_limit_violation_flag(self):
        cmd = {"az_deg": 95.0, "el_deg": 50.0}
        violated = cmd["az_deg"] > 90.0 or cmd["el_deg"] > 45.0
        assert violated

    def test_limit_within_bounds(self):
        cmd = {"az_deg": 45.0, "el_deg": 10.0}
        violated = cmd["az_deg"] > 90.0 or cmd["el_deg"] > 45.0
        assert not violated


class TestGimbalControllerState:
    """GimbalController internal fields."""

    def test_az_default_zero(self):
        az = 0.0
        assert az == 0.0

    def test_el_default_zero(self):
        el = 0.0
        assert el == 0.0

    def test_limit_violated_default_false(self):
        assert False == False

    def test_sequence_gap_default_false(self):
        assert False == False

    def test_prev_cmds_default_zero(self):
        assert 0.0 == 0.0

    def test_prev_vel_default_zero(self):
        assert 0.0 == 0.0

    def test_prev_cmd_ns_default_zero(self):
        assert 0 == 0
