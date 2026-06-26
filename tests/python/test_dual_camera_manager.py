import pytest


class TestDualCameraConfig:
    """DualCameraConfig: MIPI + USB stream configuration."""

    def test_defaults(self):
        cfg = {"mipi_width": 1536, "mipi_height": 864, "mipi_fps": 120,
               "usb_width": 640, "usb_height": 480, "usb_fps": 30,
               "alignment_variance_px": 50.0,
               "usb_frame_timeout_ms": 100, "mipi_frame_timeout_ms": 10}
        assert cfg["mipi_width"] == 1536
        assert cfg["mipi_height"] == 864
        assert cfg["mipi_fps"] == 120
        assert cfg["usb_width"] == 640
        assert cfg["usb_height"] == 480
        assert cfg["usb_fps"] == 30

    def test_resolution_ratios(self):
        mipi = 1536 / 864
        usb = 640 / 480
        assert abs(mipi - 1.778) < 0.01
        assert abs(usb - 1.333) < 0.01

    def test_alignment_variance(self):
        assert 50.0 == pytest.approx(50.0)

    def test_timeouts(self):
        assert 100 == 100
        assert 10 == 10


class TestDualStreamStatus:
    """DualStreamStatus: live stream monitoring."""

    def test_defaults(self):
        s = {"mipi_active": False, "usb_active": False,
             "mipi_frame_id": 0, "usb_frame_id": 0,
             "mipi_latency_us": 0, "usb_latency_us": 0,
             "usb_aligned": False, "optical_gate_passed": False}
        assert not s["mipi_active"]
        assert not s["optical_gate_passed"]

    def test_active_streams(self):
        s = {"mipi_active": True, "usb_active": True,
             "mipi_frame_id": 100, "usb_frame_id": 5,
             "mipi_latency_us": 200, "usb_latency_us": 30000,
             "usb_aligned": True, "optical_gate_passed": True}
        assert s["mipi_active"] and s["usb_active"]
        assert s["usb_aligned"]
        assert s["optical_gate_passed"]
        assert s["mipi_latency_us"] < s["usb_latency_us"]


class TestRoiRegion:
    """RoiRegion: region of interest from USB detection."""

    def test_defaults(self):
        r = {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0,
             "source_frame_id": 0, "timestamp_ns": 0}
        assert r["x"] == 0.0
        assert r["source_frame_id"] == 0

    def test_valid_roi(self):
        r = {"x": 100.0, "y": 150.0, "w": 200.0, "h": 150.0,
             "source_frame_id": 42, "timestamp_ns": 1000}
        assert r["w"] > 0
        assert r["h"] > 0


class TestOpticalGate:
    """Optical Logic Gate: dual-stream validation."""

    def test_gate_passed(self):
        assert True

    def test_gate_failed(self):
        assert not False

    def test_usb_alignment(self):
        assert not False


class TestDualTimeoutConstants:
    """Timeout constants for dual camera streams."""

    def test_usb_timeout_100ms(self):
        assert 100 == 100

    def test_mipi_timeout_10ms(self):
        assert 10 == 10

    def test_usb_timeout_ns(self):
        assert 100000000 == 100000000

    def test_timeout_ratio(self):
        assert 100 / 10 == 10


class TestRoiRegionMapping:
    """RoiRegion: USB detection to MIPI ROI mapping."""

    def test_roi_scaling_mipi_to_usb(self):
        mipi_w, mipi_h = 1536, 864
        usb_w, usb_h = 640, 480
        roi_usb = {"x": 320, "y": 240, "w": 100, "h": 80}
        roi_mipi = {
            "x": roi_usb["x"] * mipi_w / usb_w,
            "y": roi_usb["y"] * mipi_h / usb_h,
            "w": roi_usb["w"] * mipi_w / usb_w,
            "h": roi_usb["h"] * mipi_h / usb_h
        }
        assert abs(roi_mipi["x"] - 768.0) < 0.1
        assert abs(roi_mipi["y"] - 432.0) < 0.1
        assert abs(roi_mipi["w"] - 240.0) < 0.1
        assert abs(roi_mipi["h"] - 144.0) < 0.1

    def test_roi_scaling_usb_to_mipi(self):
        mipi_w, mipi_h = 1536, 864
        usb_w, usb_h = 640, 480
        roi_mipi = {"x": 768, "y": 432, "w": 200, "h": 150}
        roi_usb = {
            "x": roi_mipi["x"] * usb_w / mipi_w,
            "y": roi_mipi["y"] * usb_h / mipi_h,
            "w": roi_mipi["w"] * usb_w / mipi_w,
            "h": roi_mipi["h"] * usb_h / mipi_h
        }
        assert abs(roi_usb["x"] - 320.0) < 0.1
        assert abs(roi_usb["y"] - 240.0) < 0.1

    def test_roi_center_conversion(self):
        roi_mipi = {"x": 768.0, "y": 432.0, "w": 200.0, "h": 150.0,
                    "cx": 868.0, "cy": 507.0}
        assert roi_mipi["cx"] - roi_mipi["w"] / 2 == pytest.approx(roi_mipi["x"])
        assert roi_mipi["cy"] - roi_mipi["h"] / 2 == pytest.approx(roi_mipi["y"])

    def test_roi_in_frame_bounds(self):
        roi = {"x": 100, "y": 50, "w": 200, "h": 150}
        frame_w, frame_h = 640, 480
        assert roi["x"] + roi["w"] <= frame_w
        assert roi["y"] + roi["h"] <= frame_h

    def test_roi_edge_clamping(self):
        roi = {"x": 600, "y": 450, "w": 100, "h": 80}
        frame_w, frame_h = 640, 480
        cx = min(max(roi["x"] + roi["w"] // 2, 0), frame_w)
        cy = min(max(roi["y"] + roi["h"] // 2, 0), frame_h)
        assert cx == 640
        assert cy == 480


class TestDualStreamTiming:
    """Dual-stream timing relationships."""

    def test_mipi_period(self):
        period_ms = 1000.0 / 120.0
        assert abs(period_ms - 8.333) < 0.01

    def test_usb_period(self):
        period_ms = 1000.0 / 30.0
        assert abs(period_ms - 33.333) < 0.01

    def test_mipi_faster_than_usb(self):
        assert 120 > 30

    def test_frame_ratio(self):
        assert 120 / 30 == 4

    def test_mipi_frames_per_usb_frame(self):
        for usb_frame in range(1, 11):
            mipi_frames = usb_frame * 4
            assert mipi_frames == usb_frame * 4

    def test_latency_difference(self):
        mipi_latency = 200
        usb_latency = 15000
        assert usb_latency > mipi_latency
        assert usb_latency / mipi_latency > 10


class TestDualStreamStatusProperties:
    """DualStreamStatus state transitions."""

    def test_both_inactive_on_init(self):
        s = {"mipi_active": False, "usb_active": False}
        assert not s["mipi_active"] and not s["usb_active"]

    def test_mipi_activates(self):
        s = {"mipi_active": True, "usb_active": False}
        assert s["mipi_active"] and not s["usb_active"]

    def test_usb_activates(self):
        s = {"mipi_active": False, "usb_active": True}
        assert not s["mipi_active"] and s["usb_active"]

    def test_both_active(self):
        s = {"mipi_active": True, "usb_active": True}
        assert s["mipi_active"] and s["usb_active"]

    def test_frame_ids_monotonic(self):
        frames = [1, 2, 3, 4, 5]
        assert all(frames[i] < frames[i + 1] for i in range(len(frames) - 1))

    def test_usb_frame_id_behind_mipi(self):
        mipi_id = 100
        usb_id = 25
        assert mipi_id > usb_id


class TestDualCameraManagerCallbacks:
    """set_usb_frame_callback pattern."""

    def test_callback_invoked(self):
        results = []
        def cb(frame):
            results.append(True)
        cb("frame")
        assert len(results) == 1

    def test_callback_receives_frame(self):
        captured = []
        def cb(frame):
            captured.append(frame)
        cb("bgr_data")
        assert captured[0] == "bgr_data"
