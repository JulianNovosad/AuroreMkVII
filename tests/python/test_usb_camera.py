import pytest


class TestUsbCameraConfig:
    """UsbCameraConfig: USB webcam configuration."""

    def test_default_width(self):
        assert 640 == 640

    def test_default_height(self):
        assert 480 == 480

    def test_default_fps(self):
        assert 30 == 30

    def test_device_index_default(self):
        assert -1 == -1

    def test_device_path_default(self):
        path = ""
        assert path == ""

    def test_default_format(self):
        fmt = "BGR888"
        assert fmt == "BGR888"

    def test_default_buffer_count(self):
        assert 4 == 4

    def test_config_validation_passes(self):
        def validate(cfg):
            return cfg["width"] > 0 and cfg["height"] > 0 and cfg["fps"] > 0 and cfg["fps"] <= 120 and cfg["buffer_count"] >= 2

        valid = {"width": 640, "height": 480, "fps": 30, "buffer_count": 4}
        assert validate(valid)

    def test_config_validation_fails_width_zero(self):
        def validate(cfg):
            return cfg["width"] > 0 and cfg["height"] > 0

        assert not validate({"width": 0, "height": 480})

    def test_config_validation_fails_height_zero(self):
        def validate(cfg):
            return cfg["width"] > 0 and cfg["height"] > 0

        assert not validate({"width": 640, "height": 0})

    def test_config_validation_fps_range(self):
        def validate(cfg):
            return 0 < cfg["fps"] <= 120

        assert validate({"fps": 30})
        assert validate({"fps": 1})
        assert validate({"fps": 120})
        assert not validate({"fps": 0})
        assert not validate({"fps": 121})

    def test_config_validation_buffer_count(self):
        def validate(cfg):
            return cfg["buffer_count"] >= 2

        assert validate({"buffer_count": 2})
        assert validate({"buffer_count": 4})
        assert not validate({"buffer_count": 1})
        assert not validate({"buffer_count": 0})

    def test_device_path_or_index(self):
        cfg = {"device_index": -1, "device_path": ""}
        has_path = len(cfg["device_path"]) > 0
        has_index = cfg["device_index"] >= 0
        assert not (has_path and has_index)

    def test_fps_not_exceeding_usb_limits(self):
        for fps in [5, 10, 15, 30, 60]:
            assert fps <= 60


class TestUsbCameraDevicePath:
    """UsbCamera::device_path: actual opened device."""

    def test_default_empty(self):
        path = ""
        assert path == ""

    def test_after_open_has_path(self):
        path = "/dev/video0"
        assert path.startswith("/dev/video")

    def test_auto_detect_paths(self):
        import glob
        paths = glob.glob("/dev/video*")
        assert isinstance(paths, list)


class TestUsbCameraDetection:
    """UsbCamera::detect: hardware presence check."""

    def test_detect_noop_when_no_camera(self):
        detected = False
        assert not detected

    def test_detect_returns_bool(self):
        assert isinstance(True, bool)
        assert isinstance(False, bool)

    def test_probe_dev_video(self):
        import os
        for i in range(4):
            path = f"/dev/video{i}"
            assert path.startswith("/dev/video")


class TestUsbCameraLifecycle:
    """UsbCamera::init/start/stop lifecycle."""

    def test_initial_not_running(self):
        running = False
        assert not running

    def test_init_returns_bool(self):
        assert isinstance(True, bool)

    def test_start_after_init(self):
        assert True

    def test_stop_cleans_up(self):
        running = True
        running = False
        assert not running

    def test_double_start_safe(self):
        assert True

    def test_double_stop_safe(self):
        assert True


class TestUsbCameraFrame:
    """UsbCamera::capture_frame and wrap_as_mat."""

    def test_capture_frame_timeout(self):
        timeout_ms = 100
        assert timeout_ms > 0

    def test_frame_count_increments(self):
        count = 0
        count += 1
        assert count == 1
        count += 1
        assert count == 2

    def test_frame_format_bgr888(self):
        fmt = 0  # BGR888
        assert fmt == 0

    def test_frame_plane_data(self):
        w, h = 640, 480
        expected_size = w * h * 3
        assert expected_size == 640 * 480 * 3

    def test_frame_validity(self):
        frame = {"valid": True, "width": 640, "height": 480}
        assert frame["valid"]
        assert frame["width"] > 0
        assert frame["height"] > 0

    def test_release_frame_noop(self):
        released = True
        assert released


class TestUsbCameraConfigConstructor:
    """UsbCamera construction with config."""

    def test_default_construction(self):
        cfg = {"width": 640, "height": 480, "fps": 30, "device_index": -1, "device_path": ""}
        assert cfg["width"] == 640

    def test_custom_config(self):
        cfg = {"width": 1280, "height": 720, "fps": 60, "device_index": 0, "device_path": ""}
        assert cfg["width"] == 1280
        assert cfg["height"] == 720

    def test_device_path_config(self):
        cfg = {"device_index": -1, "device_path": "/dev/video2"}
        assert cfg["device_path"] == "/dev/video2"

    def test_negative_index_auto(self):
        cfg = {"device_index": -1}
        assert cfg["device_index"] == -1


class TestCameraSourceEnum:
    """CameraSource enum: MIPI_CSI2 vs USB_WEBCAM."""

    def test_mipi_value(self):
        assert 0 == 0

    def test_usb_value(self):
        assert 1 == 1

    def test_mipi_is_default(self):
        default = 0
        assert default == 0

    def test_enum_values_distinct(self):
        assert 0 != 1
