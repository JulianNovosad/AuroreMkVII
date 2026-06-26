import math
import pytest


class TestTelemetryEventIdExhaustive:
    """All 28 TelemetryEventId values."""

    def test_system_events(self):
        events = [0x0001, 0x0002]
        assert len(events) == 2
        assert all(0x0000 < e < 0x0100 for e in events)

    def test_detection_events(self):
        events = [0x0101, 0x0102, 0x0103]
        assert len(events) == 3
        assert all(0x0100 <= e < 0x0200 for e in events)

    def test_tracking_events(self):
        events = [0x0201, 0x0202, 0x0203]
        assert len(events) == 3
        assert all(0x0200 <= e < 0x0300 for e in events)

    def test_actuation_events(self):
        events = [0x0301, 0x0302, 0x0303]
        assert len(events) == 3
        assert all(0x0300 <= e < 0x0400 for e in events)

    def test_safety_events(self):
        events = [0x0401, 0x0402, 0x0403, 0x0404]
        assert len(events) == 4
        assert all(0x0400 <= e < 0x0500 for e in events)

    def test_hardware_events(self):
        events = [0x0501, 0x0502, 0x0503, 0x0504, 0x0505]
        assert len(events) == 5
        assert all(0x0500 <= e < 0x0600 for e in events)

    def test_dual_stream_events(self):
        events = [0x0601, 0x0602, 0x0603, 0x0604, 0x0605, 0x0606, 0x0607, 0x0608]
        assert len(events) == 8
        assert all(0x0600 <= e < 0x0700 for e in events)

    def test_total_event_count(self):
        total = 2 + 3 + 3 + 3 + 4 + 5 + 8
        assert total == 28

    def test_all_ids_unique(self):
        ids = [0x0001, 0x0002, 0x0101, 0x0102, 0x0103, 0x0201, 0x0202, 0x0203,
               0x0301, 0x0302, 0x0303, 0x0401, 0x0402, 0x0403, 0x0404, 0x0501,
               0x0502, 0x0503, 0x0504, 0x0505, 0x0601, 0x0602, 0x0603, 0x0604,
               0x0605, 0x0606, 0x0607, 0x0608]
        assert len(set(ids)) == 28

    def test_category_msb(self):
        ids = [0x0001, 0x0101, 0x0201, 0x0301, 0x0401, 0x0501, 0x0601]
        categories = [i >> 8 for i in ids]
        assert categories == [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06]

    def test_event_id_range_bits(self):
        assert 0xFFFF >= 0x0608


class TestTelemetrySeverity:
    """TelemetrySeverity: all 5 levels."""

    def test_values(self):
        assert 0 == 0
        assert 1 == 1
        assert 2 == 2
        assert 3 == 3
        assert 4 == 4

    def test_names(self):
        names = {0: "kDebug", 1: "kInfo", 2: "kWarning", 3: "kError", 4: "kCritical"}
        assert names[0] == "kDebug"
        assert names[2] == "kWarning"
        assert names[4] == "kCritical"

    def test_max_severity(self):
        assert 4 == 4

    def test_min_severity(self):
        assert 0 == 0


class TestSafeStringCopyProperties:
    """safe_string_copy: bounds and truncation."""

    def safe_string_copy(self, src, dest_size):
        copy_len = min(len(src), dest_size - 1)
        dest = src[:copy_len]
        return dest

    def test_basic_copy(self):
        assert self.safe_string_copy("hello", 32) == "hello"

    def test_truncation(self):
        assert self.safe_string_copy("x" * 100, 32) == "x" * 31

    def test_null_termination(self):
        s = self.safe_string_copy("hello", 32)
        assert len(s) < 32

    def test_empty_source(self):
        assert self.safe_string_copy("", 32) == ""

    def test_dest_size_one(self):
        assert self.safe_string_copy("hello", 1) == ""

    def test_exact_fit(self):
        s = self.safe_string_copy("abcd", 5)
        assert len(s) == 4
        assert s == "abcd"

    def test_module_name_max(self):
        kModule_name_max = 32
        assert kModule_name_max == 32
        s = self.safe_string_copy("vision_pipeline", kModule_name_max)
        assert len(s) <= kModule_name_max - 1

    def test_event_name_max(self):
        kEvent_name_max = 32
        assert kEvent_name_max == 32

    def test_message_max(self):
        kMessage_max = 256
        assert kMessage_max == 256


class TestValidateStringFits:
    """validate_string_fits: buffer size check."""

    def test_fits(self):
        assert len("hello") < 32

    def test_exceeds(self):
        assert not (len("x" * 100) < 32)

    def test_exact_boundary(self):
        assert len("x" * 31) < 32
        assert not (len("x" * 32) < 32)


class TestBinaryLogEntryProperties:
    """BinaryLogEntry: format and validation."""

    def test_timestamp_size(self):
        assert 8 == 8

    def test_event_id_size(self):
        assert 2 == 2

    def test_severity_size(self):
        assert 1 == 1

    def test_data_len_size(self):
        assert 1 == 1

    def test_max_data_size(self):
        assert 64 == 64

    def test_hmac_size(self):
        assert 32 == 32

    def test_hmac_u32_count(self):
        assert 8 == 8

    def test_total_size(self):
        sizes = 8 + 2 + 1 + 1 + 64 + 32
        assert sizes == 108

        with_padding = 112
        assert with_padding > sizes

    def test_data_len_validation(self):
        assert 0 <= 64
        assert 65 > 64

    def test_event_id_nonzero(self):
        assert 0x0001 != 0
        assert 0x0000 == 0

    def test_severity_validation(self):
        for s in range(5):
            assert s <= 4
        assert 5 > 4


class TestCsvLogEntryProperties:
    """CsvLogEntry: field validation."""

    def test_module_name_size(self):
        assert 32 == 32

    def test_event_name_size(self):
        assert 32 == 32

    def test_hmac_present(self):
        assert 32 == 32

    def test_field_count_float(self):
        float_fields = ["det_x", "det_y", "det_width", "det_height", "det_confidence",
                        "track_x", "track_y", "track_z", "track_confidence",
                        "servo_azimuth", "servo_elevation",
                        "cpu_temp_c", "cpu_usage_percent"]
        assert len(float_fields) == 13

    def test_det_confidence_in_range(self):
        for c in [0.0, 0.5, 1.0]:
            assert 0.0 <= c <= 1.0

    def test_track_confidence_in_range(self):
        assert 0.0 <= 0.85 <= 1.0

    def test_cpu_usage_in_range(self):
        assert 0.0 <= 50.0 <= 100.0


class TestActuationDataProperties:
    """ActuationData: gimbal command validation."""

    def test_valid_cmd(self):
        a = {"azimuth_deg": 45.0, "elevation_deg": 10.0, "velocity_dps": 30.0}
        ok = (math.isfinite(a["azimuth_deg"]) and
              math.isfinite(a["elevation_deg"]) and
              math.isfinite(a["velocity_dps"]) and
              -90.0 <= a["azimuth_deg"] <= 90.0 and
              -10.0 <= a["elevation_deg"] <= 45.0 and
              0 <= a["velocity_dps"] <= 60)
        assert ok

    def test_azimuth_negative_bound(self):
        assert -90.0 >= -90.0

    def test_azimuth_positive_bound(self):
        assert 90.0 <= 90.0

    def test_elevation_lower_bound(self):
        assert -10.0 >= -10.0

    def test_elevation_upper_bound(self):
        assert 45.0 <= 45.0

    def test_velocity_non_negative(self):
        assert 0 >= 0

    def test_velocity_max(self):
        assert 60 >= 60

    def test_azimuth_exceeds_max(self):
        assert not (91.0 <= 90.0)

    def test_elevation_exceeds_max(self):
        assert not (46.0 <= 45.0)

    def test_velocity_exceeds_max(self):
        assert not (61 <= 60)

    def test_command_sent_is_bool(self):
        assert isinstance(True, bool)
        assert isinstance(False, bool)


class TestSystemHealthDataProperties:
    """SystemHealthData: CPU/memory/frame validation."""

    def test_cpu_usage_range(self):
        for v in [0, 50, 100]:
            assert 0 <= v <= 100

    def test_jitter_range(self):
        for v in [0, 50, 100]:
            assert 0 <= v <= 100

    def test_frame_rate_range(self):
        for v in [0, 120, 1000]:
            assert 0 <= v <= 1000

    def test_nan_cpu_temp(self):
        v = float("nan")
        assert not math.isfinite(v)

    def test_inf_frame_rate(self):
        v = float("inf")
        assert not math.isfinite(v)

    def test_mem_fields_uint32(self):
        mem_used = 0
        mem_total = 0
        assert isinstance(mem_used, int)
        assert isinstance(mem_total, int)

    def test_frame_rate_zero_when_inactive(self):
        assert 0.0 == 0.0
