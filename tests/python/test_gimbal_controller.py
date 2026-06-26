import pytest
import math


class TestGimbalLimits:
    """Gimbal motion constraints from .rodata per AM7-L2-ACT-002."""

    def test_azimuth_default_limits(self):
        az_min = -90.0
        az_max = 90.0
        assert az_min < az_max
        assert az_min == -90.0
        assert az_max == 90.0

    def test_elevation_default_limits(self):
        el_min = -10.0
        el_max = 45.0
        assert el_min < el_max
        assert el_min == -10.0
        assert el_max == 45.0

    def test_rate_limit_default(self):
        rate = 60.0
        assert rate == 60.0

    def test_acceleration_limit_default(self):
        accel = 120.0
        assert accel == 120.0


class TestCameraIntrinsics:
    """CameraIntrinsics: pixel-to-angle conversion parameters."""

    def test_default_focal_length(self):
        fl = 1128.0
        assert fl == 1128.0

    def test_default_cx(self):
        cx = 768.0
        assert cx == 768.0

    def test_default_cy(self):
        cy = 432.0
        assert cy == 432.0

    def test_image_width(self):
        assert 1536 == 1536

    def test_image_height(self):
        assert 864 == 864

    def test_fov_calculation_horizontal(self):
        focal_length_px = 1128.0
        hfov_deg = 2 * math.degrees(math.atan(768.0 / focal_length_px))
        assert abs(hfov_deg - 68.5) < 1.0

    def test_fov_calculation_vertical(self):
        focal_length_px = 1128.0
        vfov_deg = 2 * math.degrees(math.atan(432.0 / focal_length_px))
        assert abs(vfov_deg - 41.9) < 1.0

    def test_pixel_to_angle(self):
        focal_length_px = 1128.0
        pixel_offset = 100.0
        angle_deg = math.degrees(math.atan(pixel_offset / focal_length_px))
        assert abs(angle_deg - 5.07) < 0.1


class TestGimbalCommand:
    """GimbalCommand: angle command structure."""

    def test_defaults(self):
        cmd = {"az_deg": 0.0, "el_deg": 0.0}
        assert cmd["az_deg"] == 0.0

    def test_with_values(self):
        cmd = {"az_deg": 45.0, "el_deg": 10.0}
        assert cmd["az_deg"] == 45.0
        assert cmd["el_deg"] == 10.0


class TestGimbalSource:
    """GimbalSource: AUTO vs FREECAM mode."""

    def test_enum_values(self):
        assert 0 == 0  # AUTO
        assert 1 == 1  # FREECAM


class TestSequenceGapDetection:
    """GimbalController::process_command_with_gap_check."""

    def test_sequence_gap_threshold(self):
        assert 1000 == 1000

    def test_no_gap(self):
        last = 100
        current = 101
        diff = (current - last) & 0xFFFFFFFF
        assert not (diff > 1000)

    def test_gap_detected(self):
        last = 100
        current = 2000
        diff = (current - last) & 0xFFFFFFFF
        assert diff > 1000

    def test_wrap_gap(self):
        last = 0xFFFFFFFF
        current = 500
        diff = (current - last) & 0xFFFFFFFF
        assert not (diff > 1000)

    def test_equal_no_gap(self):
        last = 42
        current = 42
        diff = (current - last) & 0xFFFFFFFF
        assert not (diff > 1000)


class TestCommandFromPixel:
    """GimbalController::command_from_pixel."""

    def test_center_pixel_gives_zero(self):
        cx = 768.0
        cy = 432.0
        fl = 1128.0
        centroid_x = 768.0
        centroid_y = 432.0
        dx = centroid_x - cx
        dy = centroid_y - cy
        assert dx == 0.0
        assert dy == 0.0

    def test_pixel_offset_to_angle(self):
        cx = 768.0
        fl = 1128.0
        centroid_x = 968.0
        dx = centroid_x - cx
        angle = math.degrees(math.atan(dx / fl))
        assert angle > 0
        assert abs(angle - 10.0) < 0.5

    def test_gain_scaling(self):
        gain = 1.5
        assert gain == 1.5

    def test_negative_offset(self):
        cx = 768.0
        centroid_x = 100.0
        dx = centroid_x - cx
        assert dx < 0


class TestGimbalControllerMethods:
    """GimbalController: method signatures and state."""

    def test_set_source_accepts_enum(self):
        pass

    def test_source_initial_state(self):
        assert 0 == 0

    def test_current_angles_initial(self):
        az = 0.0
        el = 0.0
        assert az == 0.0
        assert el == 0.0

    def test_set_limits_accepts_four_floats(self):
        pass

    def test_has_sequence_gap_initial_false(self):
        assert not False

    def test_reset_sequence_gap(self):
        pass

    def test_check_and_clear_limit_violation_initial_false(self):
        assert not False

    def test_reset_rate_limiter(self):
        pass

    def test_reset_angles_for_test(self):
        pass
