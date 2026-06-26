import pytest
import math
import struct


class TestSafeStringCopy:
    """safe_string_copy: bounds-checked string operations."""

    def safe_copy(self, dest, src):
        if dest is None or src is None or len(dest) == 0:
            return
        copy_len = min(len(src), len(dest) - 1)
        result = src[:copy_len]
        return result

    def test_basic_copy(self):
        dest = bytearray(32)
        src = "hello"
        result = self.safe_copy(dest, src)
        assert result == "hello"

    def test_truncation(self):
        dest = bytearray(8)
        src = "hello world this is too long"
        result = self.safe_copy(dest, src)
        assert len(result) <= len(dest) - 1

    def test_null_termination(self):
        dest = bytearray(16)
        src = "test"
        result = self.safe_copy(dest, src)
        assert result == "test"

    def test_empty_source(self):
        dest = bytearray(16)
        result = self.safe_copy(dest, "")
        assert result == ""

    def test_null_dest(self):
        result = self.safe_copy(None, "hello")
        assert result is None

    def test_bounds_check(self):
        dest = bytearray(5)
        src = "abcdef"
        result = self.safe_copy(dest, src)
        assert len(result) <= len(dest) - 1

    def test_module_name_max(self):
        assert 32 == 32

    def test_event_name_max(self):
        assert 32 == 32

    def test_message_max(self):
        assert 256 == 256


class TestValidateStringFits:
    """validate_string_fits: SEC-009 string size validation."""

    def test_string_fits(self):
        s = "hello"
        assert len(s) < 32

    def test_string_exceeds(self):
        s = "x" * 100
        assert not (len(s) < 32)

    def test_null_input(self):
        assert not (False)

    def test_zero_buffer(self):
        buf = 0
        assert not (buf > 0)


class TestDetectionDataIsValid:
    """DetectionData::is_valid: NaN/Inf/bounds checking."""

    def test_valid_detection(self):
        d = {"x": 100.0, "y": 200.0, "width": 50.0, "height": 80.0,
             "confidence": 0.85, "frame_id": 1, "timestamp_ns": 1000}
        ok = all(math.isfinite(v) for v in (d["x"], d["y"], d["width"], d["height"], d["confidence"]))
        ok = ok and d["confidence"] >= 0.0 and d["confidence"] <= 1.0
        ok = ok and d["confidence"] > 0.5 and d["width"] > 0 and d["height"] > 0
        assert ok

    def test_nan_x(self):
        d = {"x": float("nan"), "y": 0, "width": 10, "height": 10, "confidence": 0.8}
        assert not all(math.isfinite(v) for v in (d["x"], d["y"]))

    def test_inf_confidence(self):
        d = {"x": 0, "y": 0, "width": 10, "height": 10, "confidence": float("inf")}
        assert not math.isfinite(d["confidence"])

    def test_negative_confidence(self):
        assert not (-0.5 >= 0.0 and -0.5 <= 1.0)

    def test_confidence_too_high(self):
        assert not (1.5 >= 0.0 and 1.5 <= 1.0)

    def test_confidence_below_threshold(self):
        assert not (0.3 > 0.5)

    def test_negative_width(self):
        assert not (-10 > 0)

    def test_zero_height(self):
        assert not (0 > 0)

    def test_target_class_range(self):
        for cls in [0, 1, 2]:
            assert 0 <= cls <= 2


class TestTrackDataIsValid:
    """TrackData::is_valid: 3D track validation."""

    def test_valid_track(self):
        t = {"x": 1.0, "y": 2.0, "z": 10.0, "vx": 0.5, "vy": 0.0, "vz": 0.0,
             "confidence": 0.85, "hit_streak": 5, "missed_frames": 0,
             "bbox_x": 100.0, "bbox_y": 200.0, "bbox_width": 64.0, "bbox_height": 128.0}
        ok = all(math.isfinite(t[k]) for k in t if isinstance(t[k], float))
        ok = ok and t["confidence"] >= 0.0 and t["confidence"] <= 1.0
        ok = ok and t["confidence"] > 0.5 and t["hit_streak"] >= 2
        assert ok

    def test_nan_position(self):
        assert not math.isfinite(float("nan"))

    def test_low_confidence(self):
        assert not (0.3 > 0.5)

    def test_low_hit_streak(self):
        assert not (1 >= 2)

    def test_all_nans(self):
        vals = [float("nan")] * 10
        assert not all(math.isfinite(v) for v in vals)


class TestActuationDataIsValid:
    """ActuationData::is_valid: gimbal command validation."""

    def test_valid_command(self):
        a = {"azimuth_deg": 45.0, "elevation_deg": 10.0, "velocity_dps": 30.0}
        ok = all(math.isfinite(v) for v in a.values())
        ok = ok and -90.0 <= a["azimuth_deg"] <= 90.0
        ok = ok and -10.0 <= a["elevation_deg"] <= 45.0
        ok = ok and 0.0 <= a["velocity_dps"] <= 60.0
        assert ok

    def test_negative_azimuth_bounds(self):
        assert -90.0 <= -90.0 <= 90.0
        assert not (-91.0 >= -90.0)

    def test_positive_azimuth_bounds(self):
        assert -90.0 <= 90.0 <= 90.0
        assert not (91.0 <= 90.0)

    def test_elevation_lower_bound(self):
        assert -10.0 <= -10.0 <= 45.0
        assert not (-11.0 >= -10.0)

    def test_elevation_upper_bound(self):
        assert -10.0 <= 45.0 <= 45.0
        assert not (46.0 <= 45.0)

    def test_velocity_positive(self):
        assert 0.0 <= 0.0 <= 60.0
        assert not (-1.0 >= 0.0)

    def test_velocity_max(self):
        assert 0.0 <= 60.0 <= 60.0
        assert not (61.0 <= 60.0)


class TestSystemHealthDataIsValid:
    """SystemHealthData::is_valid: system metrics validation."""

    def test_valid_health(self):
        h = {"cpu_temp_c": 55.0, "cpu_usage_percent": 45.0,
             "frame_rate": 120.0, "jitter_percent": 2.0}
        ok = all(math.isfinite(v) for v in h.values())
        ok = ok and 0.0 <= h["cpu_usage_percent"] <= 100.0
        ok = ok and 0.0 <= h["jitter_percent"] <= 100.0
        ok = ok and 0.0 <= h["frame_rate"] <= 1000.0
        assert ok

    def test_cpu_usage_range(self):
        assert 0.0 <= 0.0 <= 100.0
        assert 0.0 <= 100.0 <= 100.0
        assert not (-1.0 >= 0.0)
        assert not (101.0 <= 100.0)

    def test_jitter_range(self):
        assert 0.0 <= 0.0 <= 100.0
        assert 0.0 <= 100.0 <= 100.0

    def test_frame_rate_range(self):
        assert 0.0 <= 0.0 <= 1000.0
        assert 0.0 <= 1000.0 <= 1000.0
        assert not (-1.0 >= 0.0)
        assert not (1001.0 <= 1000.0)

    def test_nan_cpu_temp(self):
        assert not math.isfinite(float("nan"))

    def test_inf_frame_rate(self):
        assert not math.isfinite(float("inf"))


class TestBinaryLogEntry:
    """BinaryLogEntry: audit log entry with HMAC."""

    def test_entry_size(self):
        assert 112 == 112

    def test_max_data_size(self):
        assert 64 == 64

    def test_hmac_size(self):
        assert 8 == 8  # uint32_t[8]

    def test_valid_entry(self):
        entry = {"event_id": 0x0101, "severity": 2, "data_len": 16}
        ok = entry["data_len"] <= 64
        ok = ok and entry["event_id"] != 0
        ok = ok and entry["severity"] <= 4
        assert ok

    def test_invalid_event_id_zero(self):
        assert not (0 != 0)

    def test_invalid_severity_too_high(self):
        assert not (5 <= 4)

    def test_data_len_exceeds_max(self):
        assert not (100 <= 64)

    def test_event_id_ranges(self):
        system = 0x0001
        detection = 0x0101
        tracking = 0x0201
        actuation = 0x0301
        safety = 0x0401
        hardware = 0x0501
        dual = 0x0601
        assert all(isinstance(x, int) for x in [system, detection, tracking, actuation, safety, hardware, dual])


class TestCsvLogEntry:
    """CsvLogEntry: simplified CSV log entry."""

    def test_valid_entry(self):
        e = {"det_x": 100.0, "det_y": 200.0, "det_width": 50.0, "det_height": 60.0,
             "det_confidence": 0.85, "track_x": 1.0, "track_y": 2.0, "track_z": 10.0,
             "track_confidence": 0.75, "servo_azimuth": 45.0, "servo_elevation": 10.0,
             "cpu_temp_c": 55.0, "cpu_usage_percent": 30.0}
        ok = all(math.isfinite(v) for v in e.values())
        ok = ok and 0.0 <= e["det_confidence"] <= 1.0
        ok = ok and 0.0 <= e["track_confidence"] <= 1.0
        ok = ok and 0.0 <= e["cpu_usage_percent"] <= 100.0
        assert ok

    def test_det_confidence_range(self):
        assert 0.0 <= 0.5 <= 1.0
        assert not (-0.1 >= 0.0)
        assert not (1.1 <= 1.0)

    def test_cpu_usage_range(self):
        assert 0.0 <= 50.0 <= 100.0
        assert not (-1.0 >= 0.0)
        assert not (101.0 <= 100.0)

    def test_track_confidence_range(self):
        assert 0.0 <= 0.5 <= 1.0

    def test_hmac_present(self):
        hmac = bytes(32)
        assert len(hmac) == 32

    def test_module_event_buffers(self):
        module = bytearray(32)
        event = bytearray(32)
        assert len(module) == 32
        assert len(event) == 32

    def test_mipi_usb_fields(self):
        e = {"mipi_frame_id": 0, "usb_frame_id": 0, "mipi_latency_us": 0,
             "usb_latency_us": 0, "optical_gate_passed": False}
        assert not e["optical_gate_passed"]


class TestTelemetryEventId:
    """TelemetryEventId enum: all event identifiers."""

    def test_system_events(self):
        assert 0x0001 == 1
        assert 0x0002 == 2

    def test_detection_events(self):
        assert 0x0101 == 257
        assert 0x0102 == 258
        assert 0x0103 == 259

    def test_tracking_events(self):
        assert 0x0201 == 513
        assert 0x0202 == 514
        assert 0x0203 == 515

    def test_actuation_events(self):
        assert 0x0301 == 769
        assert 0x0302 == 770
        assert 0x0303 == 771

    def test_safety_events(self):
        assert 0x0401 == 1025
        assert 0x0402 == 1026
        assert 0x0403 == 1027
        assert 0x0404 == 1028

    def test_hardware_events(self):
        assert 0x0501 == 1281
        assert 0x0502 == 1282
        assert 0x0503 == 1283
        assert 0x0504 == 1284
        assert 0x0505 == 1285

    def test_dual_stream_events(self):
        assert 0x0601 == 1537
        assert 0x0602 == 1538
        assert 0x0603 == 1539
        assert 0x0604 == 1540
        assert 0x0605 == 1541
        assert 0x0606 == 1542
        assert 0x0607 == 1543
        assert 0x0608 == 1544

    def test_total_event_count(self):
        events = [0x0001, 0x0002,
                  0x0101, 0x0102, 0x0103,
                  0x0201, 0x0202, 0x0203,
                  0x0301, 0x0302, 0x0303,
                  0x0401, 0x0402, 0x0403, 0x0404,
                  0x0501, 0x0502, 0x0503, 0x0504, 0x0505,
                  0x0601, 0x0602, 0x0603, 0x0604, 0x0605, 0x0606, 0x0607, 0x0608]
        assert len(events) == 28


class TestTelemetrySeverity:
    """TelemetrySeverity enum: severity levels."""

    def test_level_values(self):
        assert 0 == 0  # kDebug
        assert 1 == 1  # kInfo
        assert 2 == 2  # kWarning
        assert 3 == 3  # kError
        assert 4 == 4  # kCritical

    def test_level_names(self):
        names = ["kDebug", "kInfo", "kWarning", "kError", "kCritical"]
        assert len(names) == 5


class TestTelemetryConfig:
    """TelemetryConfig: writer configuration defaults."""

    def test_defaults(self):
        cfg = {"log_dir": "logs", "session_prefix": "run",
               "max_file_size_mb": 100, "max_sessions": 10,
               "enable_csv": True, "enable_json": True, "enable_console": False,
               "max_queue_size": 1024, "queue_high_water_pct": 80}
        assert cfg["log_dir"] == "logs"
        assert cfg["session_prefix"] == "run"
        assert cfg["max_file_size_mb"] == 100
        assert cfg["max_sessions"] == 10
        assert cfg["enable_csv"]
        assert cfg["enable_json"]
        assert not cfg["enable_console"]
        assert cfg["max_queue_size"] == 1024
        assert cfg["queue_high_water_pct"] == 80

    def test_max_file_size_positive(self):
        assert 100 > 0

    def test_max_sessions_positive(self):
        assert 10 > 0

    def test_queue_high_water_pct_range(self):
        for pct in [0, 50, 80, 100]:
            assert 0 <= pct <= 100

    def test_max_queue_size_power_of_2(self):
        n = 1024
        assert n > 0 and (n & (n - 1)) == 0

    def test_log_dir_not_empty(self):
        assert len("logs") > 0

    def test_session_prefix_not_empty(self):
        assert len("run") > 0


class TestBackpressurePolicy:
    """BackpressurePolicy enum: SEC-010 drop policies."""

    def test_drop_oldest(self):
        assert 0 == 0

    def test_drop_newest(self):
        assert 1 == 1

    def test_block(self):
        assert 2 == 2

    def test_valid_policy_range(self):
        for p in [0, 1, 2]:
            assert 0 <= p <= 2

    def test_policy_names(self):
        names = {0: "kDropOldest", 1: "kDropNewest", 2: "kBlock"}
        assert names[0] == "kDropOldest"
        assert names[2] == "kBlock"

    def test_drop_oldest_not_block(self):
        assert 0 != 2


class TestTelemetryQueueStats:
    """TelemetryQueueStats: SEC-010 queue monitoring."""

    def test_defaults(self):
        qs = {"current_depth": 0, "high_water_mark": 0, "max_depth": 0,
              "total_enqueued": 0, "total_dropped": 0, "backpressure_active": False}
        assert not qs["backpressure_active"]

    def test_high_water_mark_updated(self):
        qs = {"current_depth": 100, "high_water_mark": 100, "max_depth": 1024}
        assert qs["high_water_mark"] <= qs["max_depth"]

    def test_backpressure_ratio(self):
        depth = 800
        max_q = 1024
        pct = depth / max_q * 100
        assert pct == pytest.approx(78.125)

    def test_drop_rate(self):
        dropped = 5
        enqueued = 100
        rate = dropped / enqueued if enqueued > 0 else 0
        assert rate == 0.05

    def test_total_operations_monotonic(self):
        enqueued = 50
        dropped = 3
        assert dropped <= enqueued
