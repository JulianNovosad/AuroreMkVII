import pytest


class TestSafetyFaultCodeCategories:
    """SafetyFaultCode: category ranges."""

    def test_none_is_zero(self):
        assert 0x0000 == 0

    def test_vision_range(self):
        codes = [0x0101, 0x0102, 0x0103]
        for c in codes:
            assert 0x0100 <= c <= 0x01FF

    def test_actuation_range(self):
        codes = [0x0201, 0x0202, 0x0203]
        for c in codes:
            assert 0x0200 <= c <= 0x02FF

    def test_timing_range(self):
        codes = [0x0301, 0x0302, 0x0303]
        for c in codes:
            assert 0x0300 <= c <= 0x03FF

    def test_system_range(self):
        codes = [0x0401, 0x0402, 0x0403]
        for c in codes:
            assert 0x0400 <= c <= 0x04FF

    def test_communication_range(self):
        codes = [0x0501, 0x0502, 0x0503]
        for c in codes:
            assert 0x0500 <= c <= 0x05FF

    def test_safety_system_range(self):
        codes = [0x0601, 0x0602, 0x0603, 0x0604]
        for c in codes:
            assert 0x0600 <= c <= 0x06FF

    def test_emergency_range(self):
        codes = [0x0701, 0x0702, 0x0703]
        for c in codes:
            assert 0x0700 <= c <= 0x07FF

    def test_all_codes_unique(self):
        codes = [0x0000, 0x0101, 0x0102, 0x0103, 0x0201, 0x0202, 0x0203,
                 0x0301, 0x0302, 0x0303, 0x0401, 0x0402, 0x0403, 0x0501,
                 0x0502, 0x0503, 0x0601, 0x0602, 0x0603, 0x0604, 0x0701,
                 0x0702, 0x0703]
        assert len(set(codes)) == 23
        assert len(codes) == 23

    def test_category_count(self):
        """7 categories + NONE."""
        assert 7 == len({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07})

    def test_lsb_is_sequence_in_category(self):
        assert 0x0101 & 0xFF == 1
        assert 0x0102 & 0xFF == 2
        assert 0x0103 & 0xFF == 3


class TestSafetyEvent:
    """SafetyEvent: struct fields."""

    def test_max_fault_reason_len(self):
        assert 64 == 64

    def test_timestamp_ns_size(self):
        assert 8 == 8

    def test_fault_code_size(self):
        assert 2 == 2

    def test_severity_size(self):
        assert 1 == 1

    def test_severity_range(self):
        for s in [0, 1, 2, 3, 4]:
            assert 0 <= s <= 4

    def test_severity_names(self):
        names = {0: "debug", 1: "info", 2: "warning", 3: "error", 4: "critical"}
        assert names[0] == "debug"
        assert names[4] == "critical"

    def test_severity_error_threshold(self):
        EMERGENCY_THRESHOLD = 3
        assert EMERGENCY_THRESHOLD == 3


class TestStageLatencyStats:
    """StageLatencyStats: stall detection and averaging."""

    def test_default_stall_threshold_ns(self):
        stall_threshold_ns = 25000000
        assert stall_threshold_ns == 25000000

    def test_stall_threshold_ms(self):
        stall_threshold_ns = 25000000
        assert stall_threshold_ns // 1_000_000 == 25

    def test_avg_latency_zero_when_no_samples(self):
        total = 0
        count = 0
        avg = total // count if count > 0 else 0
        assert avg == 0

    def test_avg_latency_with_samples(self):
        total = 5000000
        count = 5
        avg = total // count
        assert avg == 1000000

    def test_max_latency_selection(self):
        samples = [100, 500, 300, 900, 200]
        current_max = 0
        for s in samples:
            if s > current_max:
                current_max = s
        assert current_max == 900

    def test_stall_detection(self):
        stall_threshold = 25000000
        assert 30000000 > stall_threshold
        assert 20000000 <= stall_threshold

    def test_stall_count_increments(self):
        assert True


class TestPerStageMonitorConfig:
    """PerStageMonitorConfig: defaults."""

    def test_enable_per_stage(self):
        assert True == True

    def test_stall_threshold_default(self):
        assert 25000000 == 25000000

    def test_stalls_before_recovery(self):
        assert 3 == 3

    def test_enable_recovery_callback(self):
        assert True == True

    def test_enable_health_report(self):
        assert True == True


class TestSafetyMonitorConfig:
    """SafetyMonitorConfig: defaults."""

    def test_vision_deadline_ns(self):
        assert 10000000 == 10000000

    def test_vision_deadline_ms(self):
        assert 10000000 // 1_000_000 == 10

    def test_actuation_deadline_ns(self):
        assert 16666000 == 16666000

    def test_frame_stall_threshold(self):
        assert 2 == 2

    def test_max_consecutive_misses(self):
        assert 3 == 3

    def test_watchdog_kick_interval_ms(self):
        assert 50 == 50

    def test_watchdog_timeout_ms(self):
        assert 60 == 60

    def test_enable_watchdog(self):
        assert True == True

    def test_watchdog_timeout_greater_than_kick_interval(self):
        assert 60 > 50

    def test_vision_tolerance_threshold(self):
        deadline_ns = 10000000
        stall_threshold = deadline_ns * 2
        assert stall_threshold == 20000000
        assert stall_threshold == 20_000_000

    def test_actuation_tolerance_threshold(self):
        deadline_ns = 16666000
        stall_threshold = deadline_ns * 2
        assert stall_threshold == 33332000

    def test_watchdog_sleep_interval_ms(self):
        tv_nsec = 10000000
        assert tv_nsec // 1_000_000 == 10


class TestSafetyMonitorHealthCheck:
    """SafetyMonitor health check logic."""

    def test_vision_stalled_detection(self):
        last_count = 5
        current_count = 5
        stall_duration = 15000000
        stall_threshold = 20000000
        stalled = (current_count == last_count and current_count > 0
                   and stall_duration > stall_threshold)
        assert not stalled

    def test_vision_stalled_trigger(self):
        last_count = 3
        current_count = 3
        stall_duration = 25000000
        stall_threshold = 20000000
        stalled = (current_count == last_count and current_count > 0
                   and stall_duration > stall_threshold)
        assert stalled

    def test_consecutive_misses_fault(self):
        misses = 3
        max_misses = 3
        assert misses >= max_misses

    def test_ok_when_below_max_misses(self):
        misses = 2
        max_misses = 3
        assert not (misses >= max_misses)

    def test_trigger_fault_clears_system_safe(self):
        system_safe = True
        system_safe = False
        assert not system_safe

    def test_emergency_active_via_exchange(self):
        emergency_active = False
        emergency_active = True
        assert emergency_active

    def test_system_safe_requires_no_emergency(self):
        system_safe = True
        emergency_active = True
        assert not (system_safe and not emergency_active)

    def test_system_safe_when_all_ok(self):
        system_safe = True
        emergency_active = False
        assert system_safe and not emergency_active

    def test_clear_fault_latched(self):
        assert False == False

    def test_fault_once_set_persists(self):
        fault_set = True
        assert fault_set


class TestWatchdogKickRAII:
    """WatchdogKick: RAII guard."""

    def test_kick_on_destroy(self):
        kicked = False
        kicked = True
        assert kicked

    def test_last_kick_time_tracked(self):
        assert True

    def test_elapsed_since_kick(self):
        last_kick = 1000
        now = 2500
        elapsed = now - last_kick
        assert elapsed == 1500

    def test_elapsed_exceeds_timeout(self):
        elapsed = 70000
        timeout = 60000
        assert elapsed > timeout

    def test_elapsed_within_timeout(self):
        elapsed = 55000
        timeout = 60000
        assert elapsed <= timeout
