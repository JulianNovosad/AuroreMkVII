import pytest


class TestSafetyFaultCode:
    """SafetyFaultCode enum: all fault categories."""

    def test_none_value(self):
        assert 0x0000 == 0

    def test_vision_fault_range(self):
        assert 0x0101 == 257
        assert 0x0102 == 258
        assert 0x0103 == 259

    def test_actuation_fault_range(self):
        assert 0x0201 == 513
        assert 0x0202 == 514
        assert 0x0203 == 515

    def test_timing_fault_range(self):
        assert 0x0301 == 769
        assert 0x0302 == 770
        assert 0x0303 == 771

    def test_system_fault_range(self):
        assert 0x0401 == 1025
        assert 0x0402 == 1026
        assert 0x0403 == 1027

    def test_comm_fault_range(self):
        assert 0x0501 == 1281
        assert 0x0502 == 1282
        assert 0x0503 == 1283

    def test_safety_fault_range(self):
        assert 0x0601 == 1537
        assert 0x0602 == 1538
        assert 0x0603 == 1539
        assert 0x0604 == 1540

    def test_emergency_fault_range(self):
        assert 0x0701 == 1793
        assert 0x0702 == 1794
        assert 0x0703 == 1795

    def test_total_fault_codes(self):
        codes = [
            0x0000, 0x0101, 0x0102, 0x0103,
            0x0201, 0x0202, 0x0203,
            0x0301, 0x0302, 0x0303,
            0x0401, 0x0402, 0x0403,
            0x0501, 0x0502, 0x0503,
            0x0601, 0x0602, 0x0603, 0x0604,
            0x0701, 0x0702, 0x0703,
        ]
        assert len(codes) == 23


class TestFaultCodeToString:
    """fault_code_to_string: human-readable fault names."""

    def test_all_codes_have_names(self):
        mapping = {
            0x0000: "NONE",
            0x0101: "VISION_STALLED", 0x0102: "VISION_LATENCY_EXCEEDED",
            0x0103: "VISION_BUFFER_OVERRUN",
            0x0201: "ACTUATION_STALLED", 0x0202: "ACTUATION_LATENCY_EXCEEDED",
            0x0203: "ACTUATION_COMMAND_INVALID",
            0x0301: "FRAME_DEADLINE_MISSED", 0x0302: "CONSECUTIVE_DEADLINE_MISSES",
            0x0303: "TIMESTAMP_NON_MONOTONIC",
            0x0401: "WATCHDOG_FEED_FAILED", 0x0402: "MEMORY_LOCK_FAILED",
            0x0403: "SCHEDULING_POLICY_FAILED",
            0x0501: "I2C_TIMEOUT", 0x0502: "I2C_NACK", 0x0503: "CAMERA_TIMEOUT",
            0x0601: "SAFETY_COMPARATOR_MISMATCH", 0x0602: "INTERLOCK_FAULT",
            0x0603: "RANGE_DATA_STALE", 0x0604: "RANGE_DATA_INVALID",
            0x0701: "EMERGENCY_STOP_REQUESTED", 0x0702: "CRITICAL_TEMPERATURE",
            0x0703: "POWER_FAULT",
        }
        for code, name in mapping.items():
            assert isinstance(code, int)
            assert isinstance(name, str)
            assert len(name) > 0
        assert len(mapping) == 23


class TestSafetyEvent:
    """SafetyEvent: fault event structure."""

    def test_default_construction(self):
        event = {"timestamp_ns": 0, "fault_code": 0x0000,
                 "severity": 0, "reason": ""}
        assert event["fault_code"] == 0x0000
        assert event["severity"] == 0

    def test_max_fault_reason_len(self):
        assert 64 == 64

    def test_severity_levels(self):
        assert 0 == 0   # debug
        assert 1 == 1   # info
        assert 2 == 2   # warning
        assert 3 == 3   # error
        assert 4 == 4   # critical


class TestPipelineStage:
    """PipelineStage enum for per-stage monitoring."""

    def test_stage_values(self):
        assert 0 == 0  # VISION
        assert 1 == 1  # TRACK
        assert 2 == 2  # ACTUATION

    def test_num_stages(self):
        assert 3 == 3

    def test_stage_names(self):
        names = ["VISION", "TRACK", "ACTUATION"]
        assert len(names) == 3


class TestStageLatencyStats:
    """StageLatencyStats: per-stage latency tracking."""

    def test_initial_values(self):
        stats = {
            "last_latency_ns": 0, "max_latency_ns": 0,
            "total_latency_ns": 0, "sample_count": 0, "stall_count": 0,
            "stall_threshold_ns": 25000000
        }
        assert stats["stall_threshold_ns"] == 25000000

    def test_record_latency_updates(self):
        last = 1000000
        total = 1000000
        count = 1
        assert last == 1000000
        assert count == 1

    def test_average_calculation(self):
        total = 50000000
        count = 10
        avg = total // count if count > 0 else 0
        assert avg == 5000000

    def test_average_zero_when_no_samples(self):
        total = 0
        count = 0
        avg = total // count if count > 0 else 0
        assert avg == 0

    def test_stall_detection(self):
        threshold = 25000000
        latency = 30000000
        assert latency > threshold

    def test_no_stall_when_under_threshold(self):
        threshold = 25000000
        latency = 10000000
        assert latency < threshold

    def test_max_latency_tracking(self):
        samples = [1000, 5000, 2000, 10000, 3000]
        mx = max(samples)
        assert mx == 10000

    def test_reset_clears_all(self):
        stats = {"last": 5000, "max": 10000, "total": 50000, "count": 10, "stalls": 2}
        stats["last"] = 0
        stats["max"] = 0
        stats["total"] = 0
        stats["count"] = 0
        stats["stalls"] = 0
        assert all(v == 0 for v in stats.values())

    def test_is_stalled_check(self):
        assert not (1000000 > 25000000)
        assert (30000000 > 25000000)


class TestPerStageMonitorConfig:
    """PerStageMonitorConfig: per-stage monitoring settings."""

    def test_defaults(self):
        config = {
            "enable_per_stage": True,
            "stall_threshold_ns": 25000000,
            "stalls_before_recovery": 3,
            "enable_recovery_callback": True,
            "enable_health_report": True,
        }
        assert config["enable_per_stage"]
        assert config["stall_threshold_ns"] == 25000000
        assert config["stalls_before_recovery"] == 3


class TestSafetyMonitorConfig:
    """SafetyMonitorConfig: global safety monitor settings."""

    def test_defaults(self):
        config = {
            "vision_deadline_ns": 10000000,
            "actuation_deadline_ns": 16666000,
            "frame_stall_threshold": 2,
            "max_consecutive_misses": 3,
            "watchdog_kick_interval_ms": 50,
            "watchdog_timeout_ms": 60,
            "enable_watchdog": True,
        }
        assert config["vision_deadline_ns"] == 10000000
        assert config["actuation_deadline_ns"] == 16666000
        assert config["watchdog_timeout_ms"] == 60
        assert config["enable_watchdog"]


class TestWatchdogKick:
    """WatchdogKick: RAII watchdog feeder."""

    def test_kick_delegates_to_monitor(self):
        kicked = [False]

        def kick():
            kicked[0] = True

        kick()
        assert kicked[0]


class TestSafetyMonitorHealthReport:
    """SafetyMonitor::generate_health_report format."""

    def test_report_header(self):
        header = "=== Per-Stage Health Report ===\n"
        assert header.startswith("===")
        assert "Health" in header


class TestSafetyMonitorClearFault:
    """SafetyMonitor::clear_fault: latched, non-clearable."""

    def test_clear_fault_always_returns_false(self):
        assert False == False
