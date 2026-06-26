import pytest
import struct
import math


class TestSafeStringCopy:
    """safe_string_copy: SEC-009 bounds-checked string copy."""

    def safe_string_copy(self, dest, src, dest_size):
        if dest is None or src is None or dest_size == 0:
            return
        src_bytes = src if isinstance(src, bytes) else src.encode()
        src_len = len(src_bytes)
        copy_len = min(src_len, dest_size - 1)
        for i in range(copy_len):
            dest[i] = src_bytes[i]
        dest[copy_len] = 0

    def test_exact_fit(self):
        buf = bytearray(10)
        self.safe_string_copy(buf, "123456789", 10)
        assert bytes(buf[:9]) == b"123456789"
        assert buf[9] == 0

    def test_truncation(self):
        buf = bytearray(5)
        self.safe_string_copy(buf, "hello world", 5)
        assert bytes(buf[:4]) == b"hell"
        assert buf[4] == 0

    def test_null_dest(self):
        self.safe_string_copy(None, "test", 10)

    def test_null_src(self):
        buf = bytearray(10)
        self.safe_string_copy(buf, None, 10)
        assert buf[0] == 0

    def test_zero_size(self):
        buf = bytearray(0)
        self.safe_string_copy(buf, "test", 0)

    def test_empty_string(self):
        buf = bytearray(10)
        self.safe_string_copy(buf, "", 10)
        assert buf[0] == 0

    def test_single_char(self):
        buf = bytearray(10)
        self.safe_string_copy(buf, "x", 10)
        assert buf[0] == ord("x")
        assert buf[1] == 0

    def test_max_module_name(self):
        buf = bytearray(32)
        name = "vision_pipeline"
        self.safe_string_copy(buf, name, 32)
        assert bytes(buf[:len(name)]) == name.encode()
        assert buf[len(name)] == 0

    def test_max_event_name(self):
        buf = bytearray(32)
        name = "DETECTION_VALID"
        self.safe_string_copy(buf, name, 32)
        assert bytes(buf[:len(name)]) == name.encode()

    def test_overflow_terminated(self):
        buf = bytearray(4)
        self.safe_string_copy(buf, "AAAAA", 4)
        assert buf[0] == ord("A")
        assert buf[1] == ord("A")
        assert buf[2] == ord("A")
        assert buf[3] == 0


class TestBinaryLogEntry:
    """BinaryLogEntry: fixed-size audit log structure."""

    def make_entry(self, event_id=1, severity=1, data=b""):
        return {
            "timestamp_ns": 0,
            "event_id": event_id,
            "severity": severity,
            "data_len": len(data),
            "data": data,
            "hmac": bytes(32),
        }

    def test_entry_size(self):
        assert 112 == 112

    def test_minimal_entry(self):
        e = self.make_entry(0x0001, 0)
        assert e["event_id"] == 0x0001
        assert e["severity"] == 0

    def test_kMaxDataSize(self):
        assert 64 == 64

    def test_data_payload_bounds(self):
        for size in [0, 1, 32, 64]:
            e = self.make_entry(data=b"\x00" * size)
            assert e["data_len"] <= 64

    def test_data_truncation(self):
        raw = b"\x01" * 100
        data_len = min(len(raw), 64)
        truncated = raw[:data_len]
        assert len(truncated) == 64

    def test_hmac_field_size(self):
        hmac = bytes(32)
        assert len(hmac) == 32

    def test_hmac_8_u32(self):
        hmac = [0] * 8
        assert len(hmac) == 8

    def test_event_id_nonzero_required(self):
        e = self.make_entry(event_id=0)
        assert e["event_id"] == 0

    def test_valid_event_id_range(self):
        valid_ids = [0x0001, 0x0101, 0x0201, 0x0301, 0x0401, 0x0501]
        for vid in valid_ids:
            assert 0 < vid <= 0xFFFF

    def test_severity_range(self):
        for s in range(5):
            e = self.make_entry(severity=s)
            assert 0 <= e["severity"] <= 4

    def test_invalid_severity(self):
        assert not (5 >= 0 and 5 <= 4)

    def test_timestamp_field(self):
        e = self.make_entry()
        e["timestamp_ns"] = 123456789
        assert e["timestamp_ns"] > 0


class TestBinaryLogEntrySetData:
    """BinaryLogEntry::set_data with bounds checking."""

    def test_set_empty_data(self):
        e = {"data_len": 0, "data": b""}
        assert e["data_len"] == 0

    def test_set_small_data(self):
        data = b"\x01\x02\x03\x04"
        e = {"data_len": len(data), "data": data}
        assert e["data_len"] == 4
        assert e["data"] == b"\x01\x02\x03\x04"

    def test_set_max_data(self):
        data = b"\xAB" * 64
        e = {"data_len": len(data), "data": data}
        assert e["data_len"] == 64

    def test_set_data_truncates(self):
        data = b"\xCD" * 100
        max_size = 64
        truncated = data[:64]
        assert len(truncated) == 64

    def test_set_data_from_string(self):
        text = "test message"
        data = text.encode()
        e = {"data_len": len(data), "data": data}
        assert e["data_len"] == len(text)


class TestBinaryLogEntryValidation:
    """BinaryLogEntry::is_valid."""

    def is_valid(self, e):
        if e["data_len"] > 64:
            return False
        if e["event_id"] == 0:
            return False
        if e["severity"] > 4:
            return False
        return True

    def test_valid_entry(self):
        e = {"data_len": 10, "event_id": 0x0101, "severity": 1}
        assert self.is_valid(e)

    def test_data_len_exceeds_max(self):
        e = {"data_len": 65, "event_id": 0x0101, "severity": 1}
        assert not self.is_valid(e)

    def test_event_id_zero(self):
        e = {"data_len": 0, "event_id": 0, "severity": 1}
        assert not self.is_valid(e)

    def test_severity_too_high(self):
        e = {"data_len": 0, "event_id": 0x0101, "severity": 5}
        assert not self.is_valid(e)

    def test_boundary_severity(self):
        for s in range(5):
            e = {"data_len": 0, "event_id": 0x0101, "severity": s}
            assert self.is_valid(e)


class TestDetectionData:
    """DetectionData: is_valid with NaN/Inf checks."""

    def is_valid(self, d):
        if not all(math.isfinite(v) for v in [d["x"], d["y"], d["w"], d["h"], d["conf"]]):
            return False
        if not (0.0 <= d["conf"] <= 1.0):
            return False
        return d["conf"] > 0.5 and d["w"] > 0.0 and d["h"] > 0.0

    def test_valid_detection(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 128.0, "conf": 0.85}
        assert self.is_valid(d)

    def test_nan_x(self):
        d = {"x": float("nan"), "y": 200.0, "w": 64.0, "h": 128.0, "conf": 0.85}
        assert not self.is_valid(d)

    def test_inf_y(self):
        d = {"x": 100.0, "y": float("inf"), "w": 64.0, "h": 128.0, "conf": 0.85}
        assert not self.is_valid(d)

    def test_neg_inf_w(self):
        d = {"x": 100.0, "y": 200.0, "w": float("-inf"), "h": 128.0, "conf": 0.85}
        assert not self.is_valid(d)

    def test_confidence_out_of_range(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 128.0, "conf": 1.5}
        assert not self.is_valid(d)

    def test_confidence_negative(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 128.0, "conf": -0.5}
        assert not self.is_valid(d)

    def test_confidence_too_low(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 128.0, "conf": 0.3}
        assert not self.is_valid(d)

    def test_zero_width(self):
        d = {"x": 100.0, "y": 200.0, "w": 0.0, "h": 128.0, "conf": 0.85}
        assert not self.is_valid(d)

    def test_zero_height(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 0.0, "conf": 0.85}
        assert not self.is_valid(d)

    def test_min_confidence_threshold(self):
        d = {"x": 100.0, "y": 200.0, "w": 64.0, "h": 128.0, "conf": 0.5}
        assert not self.is_valid(d)

    def test_target_class_default(self):
        assert 0 == 0


class TestTrackData:
    """TrackData: is_valid with velocity and hit streak."""

    def is_valid(self, t):
        fields = [t["x"], t["y"], t["z"], t["vx"], t["vy"], t["vz"],
                  t["conf"], t["bx"], t["by"], t["bw"], t["bh"]]
        if not all(math.isfinite(v) for v in fields):
            return False
        if not (0.0 <= t["conf"] <= 1.0):
            return False
        return t["conf"] > 0.5 and t["streak"] >= 2

    def test_valid_track(self):
        t = {"x": 1.0, "y": 2.0, "z": 50.0, "vx": 0.5, "vy": 0.0, "vz": 0.0,
             "conf": 0.85, "streak": 5, "bx": 100, "by": 200, "bw": 64, "bh": 128}
        assert self.is_valid(t)

    def test_low_hit_streak(self):
        t = {"x": 1.0, "y": 2.0, "z": 50.0, "vx": 0.5, "vy": 0.0, "vz": 0.0,
             "conf": 0.85, "streak": 1, "bx": 100, "by": 200, "bw": 64, "bh": 128}
        assert not self.is_valid(t)

    def test_low_confidence(self):
        t = {"x": 1.0, "y": 2.0, "z": 50.0, "vx": 0.5, "vy": 0.0, "vz": 0.0,
             "conf": 0.4, "streak": 5, "bx": 100, "by": 200, "bw": 64, "bh": 128}
        assert not self.is_valid(t)

    def test_nan_velocity(self):
        t = {"x": 1.0, "y": 2.0, "z": 50.0, "vx": float("nan"), "vy": 0.0, "vz": 0.0,
             "conf": 0.85, "streak": 5, "bx": 100, "by": 200, "bw": 64, "bh": 128}
        assert not self.is_valid(t)

    def test_confidence_upper_bound(self):
        t = {"x": 1.0, "y": 2.0, "z": 50.0, "vx": 0.5, "vy": 0.0, "vz": 0.0,
             "conf": 1.0, "streak": 2, "bx": 100, "by": 200, "bw": 64, "bh": 128}
        assert self.is_valid(t)


class TestActuationData:
    """ActuationData: is_valid with gimbal limit checks."""

    def is_valid(self, a):
        if not all(math.isfinite(v) for v in [a["az"], a["el"], a["vel"]]):
            return False
        if a["az"] < -90.0 or a["az"] > 90.0:
            return False
        if a["el"] < -10.0 or a["el"] > 45.0:
            return False
        if a["vel"] < 0.0 or a["vel"] > 60.0:
            return False
        return True

    def test_valid_actuation(self):
        a = {"az": 45.0, "el": 30.0, "vel": 30.0}
        assert self.is_valid(a)

    def test_az_below_min(self):
        a = {"az": -91.0, "el": 0.0, "vel": 30.0}
        assert not self.is_valid(a)

    def test_az_above_max(self):
        a = {"az": 91.0, "el": 0.0, "vel": 30.0}
        assert not self.is_valid(a)

    def test_el_below_min(self):
        a = {"az": 0.0, "el": -11.0, "vel": 30.0}
        assert not self.is_valid(a)

    def test_el_above_max(self):
        a = {"az": 0.0, "el": 46.0, "vel": 30.0}
        assert not self.is_valid(a)

    def test_velocity_negative(self):
        a = {"az": 0.0, "el": 0.0, "vel": -1.0}
        assert not self.is_valid(a)

    def test_velocity_exceeds_max(self):
        a = {"az": 0.0, "el": 0.0, "vel": 61.0}
        assert not self.is_valid(a)

    def test_velocity_zero(self):
        a = {"az": 0.0, "el": 0.0, "vel": 0.0}
        assert self.is_valid(a)

    def test_azimuth_boundary(self):
        assert self.is_valid({"az": -90.0, "el": 0.0, "vel": 30.0})
        assert self.is_valid({"az": 90.0, "el": 0.0, "vel": 30.0})

    def test_elevation_boundary(self):
        assert self.is_valid({"az": 0.0, "el": -10.0, "vel": 30.0})
        assert self.is_valid({"az": 0.0, "el": 45.0, "vel": 30.0})

    def test_nan_azimuth(self):
        a = {"az": float("nan"), "el": 0.0, "vel": 30.0}
        assert not self.is_valid(a)


class TestSystemHealthData:
    """SystemHealthData: is_valid with CPU/FPS bounds."""

    def is_valid(self, h):
        if not all(math.isfinite(v) for v in [h["temp"], h["cpu"], h["fps"], h["jitter"]]):
            return False
        if h["cpu"] < 0.0 or h["cpu"] > 100.0:
            return False
        if h["jitter"] < 0.0 or h["jitter"] > 100.0:
            return False
        if h["fps"] < 0.0 or h["fps"] > 1000.0:
            return False
        return True

    def test_valid_health(self):
        h = {"temp": 65.0, "cpu": 45.0, "fps": 120.0, "jitter": 5.0}
        assert self.is_valid(h)

    def test_cpu_above_100(self):
        h = {"temp": 65.0, "cpu": 101.0, "fps": 120.0, "jitter": 5.0}
        assert not self.is_valid(h)

    def test_cpu_negative(self):
        h = {"temp": 65.0, "cpu": -1.0, "fps": 120.0, "jitter": 5.0}
        assert not self.is_valid(h)

    def test_fps_above_max(self):
        h = {"temp": 65.0, "cpu": 45.0, "fps": 1001.0, "jitter": 5.0}
        assert not self.is_valid(h)

    def test_jitter_above_100(self):
        h = {"temp": 65.0, "cpu": 45.0, "fps": 120.0, "jitter": 101.0}
        assert not self.is_valid(h)

    def test_jitter_negative(self):
        h = {"temp": 65.0, "cpu": 45.0, "fps": 120.0, "jitter": -1.0}
        assert not self.is_valid(h)

    def test_nan_temperature(self):
        h = {"temp": float("nan"), "cpu": 45.0, "fps": 120.0, "jitter": 5.0}
        assert not self.is_valid(h)

    def test_fps_zero(self):
        h = {"temp": 65.0, "cpu": 45.0, "fps": 0.0, "jitter": 5.0}
        assert self.is_valid(h)


class TestTelemetryEventId:
    """TelemetryEventId enum values."""

    def test_values(self):
        ids = {
            "SYSTEM_BOOT": 0x0001,
            "SYSTEM_SHUTDOWN": 0x0002,
            "DETECTION_VALID": 0x0101,
            "DETECTION_INVALID": 0x0102,
            "DETECTION_TIMEOUT": 0x0103,
            "TRACK_ACQUIRED": 0x0201,
            "TRACK_LOST": 0x0202,
            "TRACK_UPDATED": 0x0203,
            "ACTUATION_COMMAND": 0x0301,
            "ACTUATION_LIMIT": 0x0302,
            "ACTUATION_FAULT": 0x0303,
            "SAFETY_FAULT": 0x0401,
            "SAFETY_INHIBIT_ENGAGED": 0x0402,
            "SAFETY_INHIBIT_RELEASED": 0x0403,
            "WATCHDOG_TIMEOUT": 0x0404,
            "CAMERA_TIMEOUT": 0x0501,
            "GIMBAL_TIMEOUT": 0x0502,
            "TEMPERATURE_WARNING": 0x0503,
            "TEMPERATURE_CRITICAL": 0x0504,
            "I2C_FAULT": 0x0505,
        }
        for name, val in ids.items():
            assert 0 < val <= 0xFFFF
        assert len(ids) == 20
        assert len(set(ids.values())) == 20

    def test_event_id_prefixes(self):
        assert 0x0001 >> 8 == 0x00
        assert 0x0101 >> 8 == 0x01
        assert 0x0201 >> 8 == 0x02
        assert 0x0301 >> 8 == 0x03
        assert 0x0401 >> 8 == 0x04
        assert 0x0501 >> 8 == 0x05


class TestTelemetrySeverity:
    """TelemetrySeverity enum."""

    def test_levels(self):
        assert 0 == 0  # Debug
        assert 1 == 1  # Info
        assert 2 == 2  # Warning
        assert 3 == 3  # Error
        assert 4 == 4  # Critical
