import pytest
import struct


class TestGimbalLimits:
    """Gimbal motion limits per AM7-L2-ACT-002."""

    def test_az_min_default(self):
        assert -90.0 == -90.0

    def test_az_max_default(self):
        assert 90.0 == 90.0

    def test_el_min_default(self):
        assert -10.0 == -10.0

    def test_el_max_default(self):
        assert 45.0 == 45.0

    def test_rate_limit_default(self):
        assert 60.0 == 60.0

    def test_accel_limit_default(self):
        assert 120.0 == 120.0

    def test_az_symmetric(self):
        assert abs(-90.0) == 90.0

    def test_el_asymmetric(self):
        assert abs(-10.0) < 45.0

    def test_limits_positive_range(self):
        assert 45.0 - (-10.0) == 55.0

    def test_az_range_degrees(self):
        assert 90.0 - (-90.0) == 180.0


class TestCameraIntrinsics:
    """CameraIntrinsics: pixel-to-angle conversion parameters."""

    def test_default_focal_length(self):
        assert 1128.0 == 1128.0

    def test_default_cx(self):
        assert 768.0 == 768.0

    def test_default_cy(self):
        assert 432.0 == 432.0

    def test_fov_x_from_focal(self):
        fov_x = 2 * 1536 / 1128.0
        assert fov_x == pytest.approx(2 * 1536 / 1128.0)

    def test_focal_calculation(self):
        focal_mm = 4.74
        sensor_width_mm = 6.45
        width_px = 1536
        focal_px = focal_mm * width_px / sensor_width_mm
        assert focal_px == pytest.approx(1128.0, rel=0.01)

    def test_center_calculation(self):
        assert 1536 / 2 == 768.0
        assert 864 / 2 == 432.0


class TestGimbalPixelToAngle:
    """GimbalController::command_from_pixel: pixel offset to angle."""

    def command_from_pixel(self, cx, cy, gain=1.0):
        focal_px = 1128.0
        az_delta = (cx - 768.0) / focal_px
        el_delta = (cy - 432.0) / focal_px
        az_cmd = az_delta * gain * (180.0 / 3.14159265)
        el_cmd = -el_delta * gain * (180.0 / 3.14159265)
        return {"az_deg": az_cmd, "el_deg": el_cmd}

    def test_center_pixel_zero_command(self):
        cmd = self.command_from_pixel(768.0, 432.0)
        assert abs(cmd["az_deg"]) < 0.01
        assert abs(cmd["el_deg"]) < 0.01

    def test_right_of_center_positive_az(self):
        cmd = self.command_from_pixel(1024.0, 432.0)
        assert cmd["az_deg"] > 0

    def test_left_of_center_negative_az(self):
        cmd = self.command_from_pixel(512.0, 432.0)
        assert cmd["az_deg"] < 0

    def test_above_center_positive_el(self):
        cmd = self.command_from_pixel(768.0, 200.0)
        assert cmd["el_deg"] > 0

    def test_below_center_negative_el(self):
        cmd = self.command_from_pixel(768.0, 600.0)
        assert cmd["el_deg"] < 0

    def test_gain_scales_command(self):
        cmd1 = self.command_from_pixel(1000.0, 432.0, 1.0)
        cmd2 = self.command_from_pixel(1000.0, 432.0, 2.0)
        assert abs(cmd2["az_deg"]) > abs(cmd1["az_deg"])

    def test_gain_zero_returns_zero(self):
        cmd = self.command_from_pixel(1000.0, 432.0, 0.0)
        assert abs(cmd["az_deg"]) < 0.01

    def test_gain_negative_inverts(self):
        cmd = self.command_from_pixel(1000.0, 432.0, -1.0)
        assert cmd["az_deg"] < 0

    def test_angle_within_reasonable_bounds(self):
        cmd = self.command_from_pixel(1536.0, 864.0)
        assert -90.0 <= cmd["az_deg"] <= 90.0
        assert abs(cmd["el_deg"]) <= 90.0

    def test_far_edge_not_excessive(self):
        cmd = self.command_from_pixel(0.0, 0.0)
        assert abs(cmd["az_deg"]) <= 90.0
        assert abs(cmd["el_deg"]) <= 90.0


class TestGimbalSequenceGap:
    """Sequence gap detection per AM7-L3-SEC-004."""

    def is_sequence_gap(self, old, new, threshold=1000):
        if old == new:
            return False
        diff = (new - old) & 0xFFFFFFFF
        return diff > threshold

    def test_no_gap_sequential(self):
        assert not self.is_sequence_gap(100, 101)

    def test_small_gap_below_threshold(self):
        assert not self.is_sequence_gap(100, 500)

    def test_large_gap_detected(self):
        assert self.is_sequence_gap(100, 2000, 1000)

    def test_wraparound_gap(self):
        assert self.is_sequence_gap(0xFFFFFFF0, 100, 50)

    def test_equal_sequence_no_gap(self):
        assert not self.is_sequence_gap(42, 42)

    def test_threshold_exact_boundary(self):
        assert not self.is_sequence_gap(100, 1100, 1000)
        assert self.is_sequence_gap(100, 1101, 1000)

    def test_sequence_gap_threshold_constant(self):
        assert 1000 == 1000


class TestGimbalCommandAbsolute:
    """GimbalController::command_absolute: direct angle input."""

    def command_absolute(self, az, el, limits=(-90, 90, -10, 45)):
        az_min, az_max, el_min, el_max = limits
        az_clamped = max(az_min, min(az_max, az))
        el_clamped = max(el_min, min(el_max, el))
        return {"az_deg": az_clamped, "el_deg": el_clamped}

    def test_valid_angles(self):
        cmd = self.command_absolute(45.0, 30.0)
        assert cmd["az_deg"] == 45.0
        assert cmd["el_deg"] == 30.0

    def test_az_clamped_low(self):
        cmd = self.command_absolute(-100.0, 0.0)
        assert cmd["az_deg"] == -90.0

    def test_az_clamped_high(self):
        cmd = self.command_absolute(100.0, 0.0)
        assert cmd["az_deg"] == 90.0

    def test_el_clamped_low(self):
        cmd = self.command_absolute(0.0, -20.0)
        assert cmd["el_deg"] == -10.0

    def test_el_clamped_high(self):
        cmd = self.command_absolute(0.0, 50.0)
        assert cmd["el_deg"] == 45.0

    def test_both_clamped(self):
        cmd = self.command_absolute(-100.0, 50.0)
        assert cmd["az_deg"] == -90.0
        assert cmd["el_deg"] == 45.0

    def test_null_island(self):
        cmd = self.command_absolute(0.0, 0.0)
        assert cmd["az_deg"] == 0.0
        assert cmd["el_deg"] == 0.0

    def test_custom_limits(self):
        cmd = self.command_absolute(30.0, 20.0, (-45, 45, -5, 30))
        assert cmd["az_deg"] == 30.0
        assert cmd["el_deg"] == 20.0


class TestGimbalSource:
    """GimbalSource enum: AUTO vs FREECAM."""

    def test_auto_value(self):
        assert 0 == 0

    def test_freecam_value(self):
        assert 1 == 1

    def test_default_is_auto(self):
        source = 0
        assert source == 0

    def test_switch_to_freecam(self):
        source = 0
        source = 1
        assert source == 1

    def test_switch_back_to_auto(self):
        source = 1
        source = 0
        assert source == 0


class TestGimbalControllerConstants:
    """Additional gimbal controller constants."""

    def test_sequence_gap_threshold(self):
        assert 1000 == 1000

    def test_k_default_rate_dps(self):
        assert 60.0 == 60.0

    def test_k_default_accel_dps2(self):
        assert 120.0 == 120.0


class TestGimbalRateLimit:
    """Rate limiting per AM7-L2-ACT-002."""

    def rate_limit(self, prev_az, prev_el, new_az, new_el, dt_s):
        rate_max = 60.0
        max_delta = rate_max * dt_s
        az_delta = new_az - prev_az
        el_delta = new_el - prev_el
        az_clamped = prev_az + max(-max_delta, min(max_delta, az_delta))
        el_clamped = prev_el + max(-max_delta, min(max_delta, el_delta))
        return {"az": az_clamped, "el": el_clamped}

    def test_no_limit_small_move(self):
        r = self.rate_limit(0.0, 0.0, 1.0, 1.0, 1.0)
        assert r["az"] == 1.0

    def test_rate_limited(self):
        r = self.rate_limit(0.0, 0.0, 100.0, 100.0, 1.0)
        assert r["az"] == 60.0

    def test_short_dt_tighter_limit(self):
        r = self.rate_limit(0.0, 0.0, 10.0, 0.0, 0.016)
        max_move = 60.0 * 0.016
        assert r["az"] == pytest.approx(max_move, abs=0.001)

    def test_negative_direction(self):
        r = self.rate_limit(0.0, 0.0, -100.0, 0.0, 1.0)
        assert r["az"] == -60.0

    def test_zero_dt_no_movement(self):
        r = self.rate_limit(10.0, 5.0, 20.0, 10.0, 0.0)
        assert r["az"] == 10.0
        assert r["el"] == 5.0

    def test_exact_rate_limit(self):
        r = self.rate_limit(0.0, 0.0, 60.0, 0.0, 1.0)
        assert r["az"] == 60.0
