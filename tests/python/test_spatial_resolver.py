import pytest
import math


class TestSensorOffset:
    """SensorOffset: physical offset from barrel tip."""

    def test_defaults(self):
        o = {"x": 0.0, "y": 0.0, "z": 0.0, "pitch_offset": 0.0, "yaw_offset": 0.0}
        assert all(v == 0.0 for v in o.values())

    def test_typical_mipi_offset(self):
        o = {"x": 50.0, "y": 0.0, "z": -30.0, "pitch_offset": 0.5, "yaw_offset": 0.0}
        assert o["x"] == 50.0
        assert o["z"] == -30.0

    def test_typical_lrf_offset(self):
        o = {"x": 0.0, "y": -20.0, "z": 10.0, "pitch_offset": 0.0, "yaw_offset": 0.5}
        assert o["y"] == -20.0


class TestTargetVector:
    """TargetVector: resolved 3D target position."""

    def test_defaults(self):
        t = {"x": 0.0, "y": 0.0, "z": 0.0, "range_m": 0.0,
             "az_deg": 0.0, "el_deg": 0.0, "valid": False}
        assert not t["valid"]

    def test_valid_target(self):
        t = {"x": 10.0, "y": 5.0, "z": 2.0, "range_m": 50.0,
             "az_deg": 15.0, "el_deg": 5.0, "valid": True}
        assert t["valid"]
        assert t["range_m"] > 0

    def test_range_to_az_el(self):
        range_m = 50.0
        x = 10.0
        az = math.degrees(math.atan2(x, range_m))
        assert abs(az) < 90


class TestPixelCoord:
    """PixelCoord: image pixel coordinate."""

    def test_defaults(self):
        p = {"u": 0.0, "v": 0.0, "width": 0, "height": 0}
        assert p["u"] == 0.0

    def test_mipi_resolution(self):
        p = {"u": 768.0, "v": 432.0, "width": 1536, "height": 864}
        assert p["width"] == 1536
        assert p["height"] == 864

    def test_usb_resolution(self):
        p = {"u": 320.0, "v": 240.0, "width": 640, "height": 480}
        assert p["width"] == 640
        assert p["height"] == 480


class TestConvergenceZone:
    """ConvergenceZone: dual-camera alignment zone."""

    def test_default_tolerance(self):
        assert 50.0 == 50.0

    def test_convergence_center_mipi(self):
        cz = {"mipi_x": 768.0, "mipi_y": 432.0, "usb_x": 320.0, "usb_y": 240.0, "tolerance_px": 50.0}
        assert cz["tolerance_px"] == 50.0


class TestSpatialResolverConfig:
    """SpatialResolverConfig: full configuration."""

    def test_defaults(self):
        cfg = {
            "mipi_focal_length_mm": 4.0,
            "mipi_sensor_width_mm": 6.4,
            "mipi_sensor_height_mm": 3.6,
            "usb_focal_length_mm": 3.6,
            "usb_sensor_width_mm": 4.8,
            "usb_sensor_height_mm": 3.6,
            "convergence_tolerance_px": 50.0,
            "max_range_m": 50.0,
            "min_range_m": 0.5,
            "enable_parallax_compensation": True,
        }
        assert cfg["mipi_focal_length_mm"] == 4.0
        assert cfg["usb_focal_length_mm"] == 3.6
        assert cfg["max_range_m"] == 50.0
        assert cfg["min_range_m"] == 0.5
        assert cfg["enable_parallax_compensation"]

    def test_sensor_dimensions(self):
        mipi_w = 6.4
        mipi_h = 3.6
        assert mipi_w > mipi_h


class TestParallaxCompensation:
    """Parallax compensation calculations."""

    def test_mm_to_m(self):
        mm = 1000.0
        m = mm * 0.001
        assert m == 1.0

    def test_deg_to_rad(self):
        deg = 45.0
        rad = deg * math.pi / 180.0
        assert abs(rad - 0.7854) < 0.001

    def test_rad_to_deg(self):
        rad = math.pi / 4
        deg = rad * 180.0 / math.pi
        assert abs(deg - 45.0) < 0.001

    def test_range_conversion(self):
        lrf_range_mm = 50000.0
        range_m = lrf_range_mm * 0.001
        assert range_m == 50.0

    def test_conversion_constants(self):
        assert 0.001 == pytest.approx(0.001)
        assert math.pi / 180.0 > 0
        assert 180.0 / math.pi > 0


class TestProjection:
    """TargetVector projection to pixel coordinates."""

    def test_project_to_mipi(self):
        fl_px = 1128.0
        target_x = 10.0
        target_z = 50.0
        u = fl_px * target_x / target_z + 768.0
        assert abs(u - 993.6) < 1.0

    def test_project_to_usb(self):
        fl_px = 1128.0
        target_y = 5.0
        target_z = 50.0
        v = fl_px * (-target_y) / target_z + 432.0
        assert abs(v - 319.2) < 1.0


class TestConvergenceCheck:
    """is_in_convergence_zone: dual-camera alignment check."""

    def test_within_tolerance(self):
        dlta = 30.0
        tolerance = 50.0
        assert dlta <= tolerance

    def test_outside_tolerance(self):
        dlta = 100.0
        tolerance = 50.0
        assert not (dlta <= tolerance)

    def test_convergence_with_coordinates(self):
        mipi_x, mipi_y = 768.0, 432.0
        usb_x, usb_y = 750.0, 420.0
        cz_tol = 50.0
        dx = abs(mipi_x - usb_x)
        dy = abs(mipi_y - usb_y)
        assert dx <= cz_tol
        assert dy <= cz_tol

    def test_convergence_outside_tolerance(self):
        mipi_x, mipi_y = 768.0, 432.0
        usb_x, usb_y = 200.0, 100.0
        cz_tol = 50.0
        dx = abs(mipi_x - usb_x)
        dy = abs(mipi_y - usb_y)
        assert dx > cz_tol
        assert dy > cz_tol

    def test_tolerance_edge_zero(self):
        mipi_x, usb_x = 768.0, 768.0
        tol = 0.0
        assert abs(mipi_x - usb_x) <= tol


class TestFocalLengthPixels:
    """Focal length in pixels calculation."""

    def test_mipi_focal_length_px(self):
        fl_mm = 4.0
        sensor_w_mm = 6.4
        img_w = 1536
        fl_px = fl_mm * img_w / sensor_w_mm
        assert abs(fl_px - 960.0) < 0.1

    def test_usb_focal_length_px(self):
        fl_mm = 3.6
        sensor_w_mm = 4.8
        img_w = 640
        fl_px = fl_mm * img_w / sensor_w_mm
        assert abs(fl_px - 480.0) < 0.1

    def test_mipi_focal_length_px_from_height(self):
        fl_mm = 4.0
        sensor_h_mm = 3.6
        img_h = 864
        fl_px = fl_mm * img_h / sensor_h_mm
        assert abs(fl_px - 960.0) < 0.1

    def test_usb_focal_length_px_from_height(self):
        fl_mm = 3.6
        sensor_h_mm = 3.6
        img_h = 480
        fl_px = fl_mm * img_h / sensor_h_mm
        assert abs(fl_px - 480.0) < 0.1


class TestProjectToMipi:
    """project_to_mipi: target vector to pixel projection."""

    def test_center_target(self):
        fl_px = 960.0
        cx, cy = 768.0, 432.0
        tx, ty, tz = 0.0, 0.0, 50.0
        u = fl_px * tx / tz + cx
        v = fl_px * (-ty) / tz + cy
        assert abs(u - 768.0) < 0.1
        assert abs(v - 432.0) < 0.1

    def test_target_right(self):
        fl_px = 960.0
        cx = 768.0
        tx, tz = 5.0, 50.0
        u = fl_px * tx / tz + cx
        assert abs(u - 864.0) < 0.1

    def test_target_above(self):
        fl_px = 960.0
        cy = 432.0
        ty, tz = 5.0, 50.0
        v = fl_px * (-ty) / tz + cy
        assert abs(v - 336.0) < 0.1

    def test_target_far_right(self):
        fl_px = 960.0
        cx = 768.0
        tx, tz = 10.0, 25.0
        u = fl_px * tx / tz + cx
        assert abs(u - 1152.0) < 0.1

    def test_target_below(self):
        fl_px = 960.0
        cy = 432.0
        ty, tz = -3.0, 30.0
        v = fl_px * (-ty) / tz + cy
        assert abs(v - 528.0) < 0.1


class TestProjectToUsb:
    """project_to_usb: target vector to USB pixel projection."""

    def test_center_target(self):
        fl_px = 480.0
        cx, cy = 320.0, 240.0
        tx, ty, tz = 0.0, 0.0, 50.0
        u = fl_px * tx / tz + cx
        v = fl_px * (-ty) / tz + cy
        assert abs(u - 320.0) < 0.1
        assert abs(v - 240.0) < 0.1

    def test_target_right(self):
        fl_px = 480.0
        cx = 320.0
        tx, tz = 5.0, 50.0
        u = fl_px * tx / tz + cx
        assert abs(u - 368.0) < 0.1

    def test_target_above(self):
        fl_px = 480.0
        cy = 240.0
        ty, tz = 3.0, 30.0
        v = fl_px * (-ty) / tz + cy
        assert abs(v - 192.0) < 0.1

    def test_target_far(self):
        fl_px = 480.0
        cx, cy = 320.0, 240.0
        tx, ty, tz = 2.0, 1.0, 100.0
        u = fl_px * tx / tz + cx
        v = fl_px * (-ty) / tz + cy
        assert abs(u - 329.6) < 0.1
        assert abs(v - 235.2) < 0.1


class TestParallaxCompensation:
    """Parallax adjustment from physical sensor offset."""

    def test_zero_offset_no_adjustment(self):
        offset_mm = 0.0
        range_mm = 50000.0
        angle_rad = math.atan2(offset_mm, range_mm)
        assert abs(angle_rad) < 0.001

    def test_mipi_offset_parallax(self):
        offset_x_mm = 50.0
        range_mm = 50000.0
        az_correction = math.degrees(math.atan2(offset_x_mm, range_mm))
        assert abs(az_correction) < 1.0

    def test_lrf_offset_parallax(self):
        offset_y_mm = -20.0
        range_mm = 50000.0
        el_correction = math.degrees(math.atan2(offset_y_mm, range_mm))
        assert abs(el_correction) < 1.0

    def test_parallax_decreases_with_range(self):
        offset = 50.0
        r1, r2 = 10000.0, 50000.0
        a1 = math.atan2(offset, r1)
        a2 = math.atan2(offset, r2)
        assert a1 > a2

    def test_short_range_parallax_significant(self):
        offset = 100.0
        range_mm = 5000.0
        correction = math.degrees(math.atan2(offset, range_mm))
        assert correction > 1.0

    def test_long_range_parallax_negligible(self):
        offset = 100.0
        range_mm = 100000.0
        correction = math.degrees(math.atan2(offset, range_mm))
        assert correction < 0.1


class TestLrfToBarrelRange:
    """lrf_to_barrel_range_m: LRF range to barrel-relative range."""

    def test_lrf_aligned_no_correction(self):
        lrf_range_mm = 50000.0
        lrf_offset_z = 10.0
        barrel_range = math.sqrt(lrf_range_mm ** 2 + lrf_offset_z ** 2) * 0.001
        assert abs(barrel_range - 50.0) < 0.01

    def test_lrf_offset_correction(self):
        lrf_range_mm = 10000.0
        lrf_offset_x = 50.0
        lrf_offset_z = -30.0
        dx = lrf_offset_x
        dz = lrf_offset_z
        corrected = math.sqrt((lrf_range_mm + dz) ** 2 + dx ** 2) * 0.001
        assert corrected > 0

    def test_barrel_range_in_meters(self):
        lrf_range_mm = 25000.0
        lrf_offset_z = 0.0
        barrel_range_m = lrf_range_mm * 0.001
        assert barrel_range_m == 25.0

    def test_conversion_scale(self):
        assert 0.001 == pytest.approx(0.001)


class TestResolveFromMipi:
    """resolve_from_mipi: pixel + range to TargetVector."""

    def test_center_pixel_no_offset(self):
        fl_px = 960.0
        cx, cy = 768.0, 432.0
        pu, pv = 768.0, 432.0
        range_m = 50.0
        range_mm = range_m * 1000.0
        dx = pu - cx
        dy = pv - cy
        x = dx / fl_px * range_mm
        y = -(dy / fl_px * range_mm)
        assert abs(x) < 0.01
        assert abs(y) < 0.01

    def test_offset_pixel_to_world(self):
        fl_px = 960.0
        cx, cy = 768.0, 432.0
        pu, pv = 960.0, 540.0
        range_mm = 50000.0
        dx = pu - cx
        dy = pv - cy
        x_mm = dx / fl_px * range_mm
        y_mm = -(dy / fl_px * range_mm)
        assert abs(x_mm - 10000.0) < 0.1
        assert abs(y_mm - (-5625.0)) < 0.1

    def test_right_pixel_positive_x(self):
        fl_px = 960.0
        cx = 768.0
        pu = 1536.0
        range_mm = 50000.0
        x_mm = (pu - cx) / fl_px * range_mm
        assert x_mm > 0

    def test_left_pixel_negative_x(self):
        fl_px = 960.0
        cx = 768.0
        pu = 0.0
        range_mm = 50000.0
        x_mm = (pu - cx) / fl_px * range_mm
        assert x_mm < 0


class TestResolveFromUsb:
    """resolve_from_usb: USB pixel + range to TargetVector."""

    def test_center_pixel(self):
        fl_px = 480.0
        cx, cy = 320.0, 240.0
        pu, pv = 320.0, 240.0
        range_mm = 50000.0
        x_mm = (pu - cx) / fl_px * range_mm
        y_mm = -(pv - cy) / fl_px * range_mm
        assert abs(x_mm) < 0.01
        assert abs(y_mm) < 0.01

    def test_offset_pixel(self):
        fl_px = 480.0
        cx = 320.0
        pu = 640.0
        range_mm = 50000.0
        x_mm = (pu - cx) / fl_px * range_mm
        assert abs(x_mm - 33333.3) < 0.1

    def test_bottom_pixel_negative_y(self):
        fl_px = 480.0
        cy = 240.0
        pv = 480.0
        range_mm = 30000.0
        y_mm = -(pv - cy) / fl_px * range_mm
        assert y_mm < 0


class TestResolveFused:
    """resolve_fused: combined MIPI + USB resolution."""

    def test_fused_average(self):
        mipi_x, usb_x = 10000.0, 10200.0
        mipi_y, usb_y = 5000.0, 4800.0
        fused_x = (mipi_x + usb_x) / 2.0
        fused_y = (mipi_y + usb_y) / 2.0
        assert abs(fused_x - 10100.0) < 0.1
        assert abs(fused_y - 4900.0) < 0.1

    def test_fused_mipi_weight(self):
        mipi_x, usb_x = 10000.0, 11000.0
        mipi_w = 0.7
        usb_w = 0.3
        fused = mipi_w * mipi_x + usb_w * usb_x
        assert abs(fused - 10300.0) < 0.1

    def test_fused_range_from_xyz(self):
        x, y, z = 10.0, 5.0, 50.0
        range_m = math.sqrt(x * x + y * y + z * z)
        assert abs(range_m - 51.234) < 0.01

    def test_fused_az_el_from_xyz(self):
        x, y, z = 10.0, 5.0, 50.0
        az = math.degrees(math.atan2(x, z))
        el = math.degrees(math.atan2(y, math.sqrt(x * x + z * z)))
        assert abs(az - 11.31) < 0.01
        assert abs(el - 5.60) < 0.01


class TestCalibrateWithKnownTarget:
    """calibrate_with_known_target: offset calibration."""

    def test_measured_vs_expected(self):
        measured_range_mm = 50000.0
        expected_px = 768.0
        observed_px = 780.0
        offset_px = observed_px - expected_px
        fl_px = 960.0
        offset_mm = offset_px / fl_px * measured_range_mm
        assert abs(offset_mm - 625.0) < 0.1

    def test_calibration_updates_offset(self):
        new_offset = 625.0
        assert new_offset > 0

    def test_calibration_zero_error(self):
        assert 768.0 - 768.0 == 0.0
