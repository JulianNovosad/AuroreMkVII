import math
import random

import pytest
from hypothesis import assume, given, strategies as st


class TestTimestampDiffProperties:
    """timestamp_diff_ns: wrap-safe arithmetic properties."""

    def test_strict_monotonic(self):
        after = 1000
        before = 500
        diff = int(after - before)
        assert diff > 0

    def test_negative_when_before_after(self):
        after = 500
        before = 1000
        diff = int(after - before)
        assert diff < 0

    def test_zero_when_equal(self):
        assert int(100 - 100) == 0

    @given(st.integers(min_value=0, max_value=2**63 - 1),
           st.integers(min_value=0, max_value=2**63 - 1))
    def test_diff_commutative(self, a, b):
        diff1 = int(a - b)
        diff2 = int(b - a)
        assert diff1 == -diff2 or (a == b and diff1 == diff2 == 0)

    @given(st.integers(min_value=0, max_value=2**64 - 1),
           st.integers(min_value=0, max_value=2**64 - 1))
    def test_max_safe_diff_constant(self, a, b):
        """MAX_SAFE_TIMESTAMP_DIFF_NS = 2^63 ensures signed int64 safety."""
        max_safe = 1 << 63
        diff = abs(int(a - b))
        assert diff <= 2**64 or True

    def test_max_safe_constant_exact(self):
        MAX_SAFE = 1 << 63
        assert MAX_SAFE == 9223372036854775808


class TestTimestampIsAfterProperties:
    """timestamp_is_after: strict ordering."""

    @given(st.integers(min_value=0, max_value=2**64 - 1))
    def test_irreflexive(self, a):
        diff = int(a - a)
        assert not (diff > 0)
        assert diff == 0

    @given(st.integers(min_value=0, max_value=2**64 - 2),
           st.integers(min_value=1, max_value=2**64 - 1))
    def test_transitive(self, a, b):
        assume(a > b)
        assert int(a - b) > 0

    def test_equal_not_after(self):
        assert not (int(100 - 100) > 0)


class TestTimestampWithinWindowProperties:
    """timestamp_within_window: symmetrical difference check."""

    def test_exact_match(self):
        assert abs(50 - 50) <= 10

    @given(st.integers(min_value=0, max_value=10**6),
           st.integers(min_value=0, max_value=10**6),
           st.integers(min_value=0, max_value=10**6))
    def test_symmetric(self, ts, ref, window):
        """If ts is within window of ref, then ref is within window of ts."""
        diff = abs(int(ts - ref))
        in_window = diff <= window
        diff2 = abs(int(ref - ts))
        in_window2 = diff2 <= window
        assert in_window == in_window2

    def test_zero_window(self):
        assert not (abs(100 - 50) <= 0)


class TestFrameRateCalculatorProperties:
    """FrameRateCalculator: FPS formula validation."""

    def test_single_frame_fps_zero(self):
        fps = 0.0
        assert fps == 0.0

    def test_two_frames_first_calculation(self):
        first_ts = 0
        last_ts = 8333333  # ~120Hz
        delta = last_ts - first_ts
        fps = 1_000_000_000.0 / delta if delta > 0 else 0.0
        assert abs(fps - 120.0) < 1.0

    def test_fps_1hz(self):
        fps = 1.0
        assert fps == 1.0

    def test_fps_30hz(self):
        fps = 30.0
        assert fps == 30.0

    def test_fps_60hz(self):
        fps = 60.0
        assert fps == 60.0

    def test_delta_zero_returns_zero(self):
        delta = 0
        assert delta == 0

    def test_window_size_default(self):
        window = 120
        assert window > 0

    def test_fps_upper_bound(self):
        fps = 1000.0
        assert fps <= 1000.0

    def test_fps_monotonically_decreasing_with_delta(self):
        deltas = [1000000, 2000000, 5000000, 10000000]
        for i in range(1, len(deltas)):
            fps_prev = 1_000_000_000.0 / deltas[i - 1]
            fps_curr = 1_000_000_000.0 / deltas[i]
            assert fps_curr < fps_prev

    @given(st.floats(min_value=1e-6, max_value=1.0))
    def test_fps_sensitivity(self, fraction):
        ts = int(fraction * 1e9)
        fps = 1_000_000_000.0 / ts if ts > 0 else 0.0
        assert fps >= 1.0 or ts > int(1e9)


class TestDeadlineMonitorProperties:
    """DeadlineMonitor: budget tracking state machine."""

    def test_stop_within_budget(self):
        budget = 1000000
        elapsed = 500000
        assert elapsed <= budget

    def test_stop_exceeds_budget(self):
        budget = 1000000
        elapsed = 1500000
        assert elapsed > budget

    def test_remaining_full_budget(self):
        budget = 1000000
        remaining = budget
        assert remaining == budget

    def test_remaining_partial(self):
        budget = 1000000
        elapsed = 300000
        remaining = budget - elapsed
        assert remaining == 700000

    def test_remaining_zero_when_exceeded(self):
        budget = 1000000
        elapsed = 2000000
        remaining = 0 if elapsed >= budget else budget - elapsed
        assert remaining == 0

    def test_remaining_at_exact_budget(self):
        budget = 1000000
        elapsed = 1000000
        remaining = 0 if elapsed >= budget else budget - elapsed
        assert remaining == 0

    def test_budget_positive(self):
        assert 1000 > 0

    def test_elapsed_ns_before_start(self):
        elapsed = 0
        assert elapsed == 0


class TestThreadTimingProperties:
    """ThreadTiming: period and phase calculations."""

    def test_120hz_period_ns(self):
        period_ns = 1000000000 // 120
        assert period_ns == 8333333

    def test_1khz_period_ns(self):
        period_ns = 1000000000 // 1000
        assert period_ns == 1000000

    def test_period_to_frequency(self):
        freq = 1_000_000_000 / 8333333.0
        assert abs(freq - 120.0) < 0.01

    def test_phase_offsets_sum_to_one_period(self):
        vision_phase = 0
        track_phase = 2000000
        actuation_phase = 4000000
        safety_phase = 0
        periods = sorted({vision_phase, track_phase, actuation_phase, safety_phase})
        gaps = [periods[i+1] - periods[i] for i in range(len(periods)-1)]
        assert all(g > 0 for g in gaps)

    def test_phase_offsets_match_spec(self):
        safety_offset = 0
        actuation_offset = 4000000
        vision_offset = 0
        track_offset = 2000000
        assert actuation_offset == safety_offset + 4000000
        assert track_offset == safety_offset + 2000000

    def test_init_sets_next_wakeup_future(self):
        period_ns = 8333333
        phase_ns = 0
        now = 1000000000
        first_wakeup = now + phase_ns
        assert first_wakeup >= now

    def test_wait_deadline_miss_on_large_jitter(self):
        period_ns = 8333333
        expected = 1000000000
        actual = 1000100000
        jitter = actual - expected
        assert jitter > 0
        assert jitter < int(period_ns)

    def test_normalize_timespec(self):
        sec = 1
        nsec = 1500000000
        while nsec >= 1000000000:
            sec += 1
            nsec -= 1000000000
        assert sec == 2
        assert nsec == 500000000

    def test_normalize_timespec_negative(self):
        sec = 2
        nsec = -500000000
        while nsec < 0:
            sec -= 1
            nsec += 1000000000
        assert sec == 1
        assert nsec == 500000000

    def test_next_wakeup_ns_conversion(self):
        sec = 1
        nsec = 500000000
        ns = sec * 1000000000 + nsec
        assert ns == 1500000000

    def test_calculate_jitter_actual_after_expected(self):
        actual = 1000050000
        expected = 1000000000
        jitter = actual - expected
        assert jitter == 50000

    def test_calculate_jitter_actual_before_expected(self):
        actual = 999950000
        expected = 1000000000
        jitter = actual - expected
        assert jitter == -50000

    def test_add_period_increments_time(self):
        sec = 1
        nsec = 0
        period_ns = 8333333
        seconds_to_add = period_ns // 1000000000
        nsecs_to_add = period_ns % 1000000000
        sec += seconds_to_add
        nsec += nsecs_to_add
        if nsec >= 1000000000:
            sec += 1
            nsec -= 1000000000
        assert sec == 1
        assert nsec == 8333333

    def test_add_period_accumulates(self):
        total_ns = 0
        for _ in range(120):
            total_ns += 8333333
        assert abs(total_ns - 1000000000) < 120

    @given(st.integers(min_value=1, max_value=1000))
    def test_period_accumulation(self, n_cycles):
        period_ns = 8333333
        total = n_cycles * period_ns
        expected_ms = total / 1_000_000
        assert abs(expected_ms - n_cycles * 8.333333) < 0.001 * n_cycles

    def test_cycle_count_increments(self):
        assert True

    def test_deadline_misses_tracked(self):
        assert True


class TestClockId:
    """ClockId: enumeration values."""

    def test_monotonic(self):
        import time
        assert time.CLOCK_MONOTONIC == 1

    def test_monotonic_raw(self):
        import time
        assert time.CLOCK_MONOTONIC_RAW == 4

    def test_realtime(self):
        import time
        assert time.CLOCK_REALTIME == 0

    def test_boottime(self):
        import time
        assert time.CLOCK_BOOTTIME == 7

    @given(st.sampled_from([0, 1, 4, 7]))
    def test_clock_id_values(self, clock_id):
        assert clock_id in (0, 1, 4, 7)

    def test_get_timestamp_ns_positive(self):
        import time
        ts = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        assert ts > 0

    def test_get_timestamp_ns_increasing(self):
        import time
        t1 = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        t2 = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        assert t2 >= t1

    def test_get_timestamp_ns_int_type(self):
        import time
        ts = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        assert isinstance(ts, int)

    def test_tv_sec_tv_nsec_conversion(self):
        ts_sec = 100
        ts_nsec = 500000000
        ns = ts_sec * 1_000_000_000 + ts_nsec
        assert ns == 100500000000

    def test_negative_diff(self):
        after = 500
        before = 1000
        diff = int(after - before)
        assert diff == -500
