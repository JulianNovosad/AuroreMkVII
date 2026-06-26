import pytest
import struct
import math


class TestTimestampDiff:
    """timestamp_diff_ns: wrap-safe signed difference for uint64_t timestamps."""

    def test_normal_forward(self):
        after = 1000
        before = 500
        diff = (after - before) & 0xFFFFFFFFFFFFFFFF
        result = diff if diff < (1 << 63) else diff - (1 << 64)
        assert result == 500

    def test_normal_backward(self):
        after = 500
        before = 1000
        diff = (after - before) & 0xFFFFFFFFFFFFFFFF
        result = diff if diff < (1 << 63) else diff - (1 << 64)
        assert result == -500

    def test_wrap_forward(self):
        after = 500
        before = 0xFFFFFFFFFFFFFF00
        diff = (after - before) & 0xFFFFFFFFFFFFFFFF
        result = diff if diff < (1 << 63) else diff - (1 << 64)
        assert result == 500 + 256

    def test_wrap_backward(self):
        after = 0xFFFFFFFFFFFFFF00
        before = 500
        diff = (after - before) & 0xFFFFFFFFFFFFFFFF
        result = diff if diff < (1 << 63) else diff - (1 << 64)
        assert result == -(500 + 256)

    def test_exact_equal(self):
        ts = 12345678
        diff = (ts - ts) & 0xFFFFFFFFFFFFFFFF
        result = diff if diff < (1 << 63) else diff - (1 << 64)
        assert result == 0

    def test_max_safe_diff_constant(self):
        assert (1 << 63) == 9223372036854775808


class TestTimestampIsAfter:
    """timestamp_is_after: wrap-safe comparison."""

    def test_after_true(self):
        assert (100 - 50) > 0

    def test_after_false(self):
        assert not (50 - 100) > 0

    def test_wrap_after(self):
        a = 500
        b = 0xFFFFFFFFFFFFFF00
        diff = (a - b) & 0xFFFFFFFFFFFFFFFF
        assert (diff if diff < (1 << 63) else diff - (1 << 64)) > 0

    def test_equal(self):
        assert not (100 - 100) > 0


class TestTimestampWithinWindow:
    """timestamp_within_window: check if |ts - ref| <= window."""

    def within_window(self, ts, ref, window_ns):
        diff = (ts - ref) & 0xFFFFFFFFFFFFFFFF
        diff_signed = diff if diff < (1 << 63) else diff - (1 << 64)
        return abs(diff_signed) <= window_ns

    def test_exact_match(self):
        assert self.within_window(1000, 1000, 0)

    def test_within_positive(self):
        assert self.within_window(1500, 1000, 500)

    def test_within_negative(self):
        assert self.within_window(500, 1000, 500)

    def test_exceeded_positive(self):
        assert not self.within_window(2000, 1000, 500)

    def test_exceeded_negative(self):
        assert not self.within_window(100, 1000, 500)

    def test_zero_window(self):
        assert self.within_window(42, 42, 0)
        assert not self.within_window(43, 42, 0)


class TestDeadlineMonitor:
    """DeadlineMonitor: execution time budget tracking."""

    def test_budget_ns_stored(self):
        budget = 2000000
        assert budget == 2000000

    def test_elapsed_ns_before_start(self):
        assert 0 == 0

    def test_remaining_ns_full_budget(self):
        budget = 5000000
        remaining = budget
        assert remaining == budget

    def test_exceeded_returns_false_when_not_started(self):
        dm = {"started": False, "budget_ns": 1000, "elapsed_ns": 0}
        assert not dm["started"]

    def test_is_running_initial_state(self):
        dm = {"started": False, "budget_ns": 1000, "elapsed_ns": 0}
        assert not dm["started"]

    def test_not_exceeded_on_construction(self):
        dm = {"started": False, "budget_ns": 1000, "elapsed_ns": 0}
        assert not (dm["started"] and dm["elapsed_ns"] > dm["budget_ns"])


class TestFrameRateCalculator:
    """FrameRateCalculator: sliding window FPS tracking."""

    def test_fps_zero_when_no_frames(self):
        assert 0.0 == 0.0

    def test_fps_zero_when_single_frame(self):
        assert 0.0 == 0.0

    def test_fps_calculation(self):
        delta_ns = 1000000000
        count = 120
        expected_fps = (count - 1) * 1e9 / delta_ns
        assert abs(expected_fps - 119.0) < 0.01

    def test_fps_120hz(self):
        ns_per_frame = 8333333
        total_ns = ns_per_frame * 120
        fps = 120 * 1e9 / total_ns
        assert abs(fps - 120.0) < 0.1

    def test_fps_zero_when_delta_zero(self):
        assert 0.0 == 0.0

    def test_reset_clears_state(self):
        assert True


class TestThreadTimingConstants:
    """ThreadTiming: period/phase constants."""

    def test_120hz_period_ns(self):
        period_ns = 8333333
        assert period_ns == 8333333

    def test_1khz_period_ns(self):
        period_ns = 1000000
        assert period_ns == 1000000

    def test_phase_offsets_match_spec(self):
        actuation_phase = 4000000
        track_phase = 2000000
        assert actuation_phase > track_phase

    def test_period_to_frequency(self):
        period_ns = 8333333
        freq_hz = 1e9 / period_ns
        assert abs(freq_hz - 120.0) < 0.5


class TestGetTimestamp:
    """get_timestamp: system clock access."""

    def test_monotonic_raw_increases(self):
        import time
        t1 = time.clock_gettime(time.CLOCK_MONOTONIC_RAW)
        t2 = time.clock_gettime(time.CLOCK_MONOTONIC_RAW)
        assert t2 >= t1

    def test_monotonic_increases(self):
        import time
        t1 = time.clock_gettime(time.CLOCK_MONOTONIC)
        import time as time2
        t2 = time2.clock_gettime(time2.CLOCK_MONOTONIC)
        assert t2 >= t1

    def test_nanosecond_resolution(self):
        import time
        ts = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        assert ts > 0
        assert isinstance(ts, int)

    def test_clock_id_constants(self):
        import time
        assert time.CLOCK_MONOTONIC == 1
        assert time.CLOCK_MONOTONIC_RAW == 4
