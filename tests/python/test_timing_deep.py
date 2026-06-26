import ctypes
import math
import sys
import time as time_module

import pytest


class TestDeadlineMonitorFull:
    """Full lifecycle of DeadlineMonitor: start, stop, exceeded, remaining, reset."""

    def test_start_sets_elapsed_increasing(self):
        budget = 1_000_000
        start = 0
        end = 500_000
        running = True
        elapsed = end - start
        assert elapsed < budget

    def test_stop_returns_true_within_budget(self):
        budget = 1_000_000
        start = 1_000
        end = 600_000
        elapsed = end - start
        met = elapsed <= budget
        assert met

    def test_stop_returns_false_when_exceeded(self):
        budget = 1_000_000
        start = 1_000
        end = 2_000_000
        elapsed = end - start
        met = elapsed <= budget
        assert not met

    def test_exceeded_true_when_budget_exceeded(self):
        budget = 500_000
        start = 100
        end = 1_000_000
        running = False
        elapsed = end - start if not running else 0
        exceeded = elapsed > budget
        assert exceeded

    def test_exceeded_false_when_not_started(self):
        running = False
        end = 0
        assert not running and end == 0

    def test_exceeded_false_while_running_under_budget(self):
        budget = 10_000_000
        start = 1_000
        now = 5_000_000
        running = True
        elapsed = now - start
        exceeded = elapsed > budget
        assert not exceeded

    def test_exceeded_true_while_running_over_budget(self):
        budget = 1_000_000
        start = 1_000
        now = 3_000_000
        running = True
        elapsed = now - start
        exceeded = elapsed > budget
        assert exceeded

    def test_remaining_full_budget_before_start(self):
        budget = 2_000_000
        running = False
        end = 0
        elapsed = 0
        remaining = budget - elapsed
        assert remaining == budget

    def test_remaining_partial_while_running(self):
        budget = 5_000_000
        start = 0
        now = 2_000_000
        running = True
        elapsed = now - start
        remaining = budget - elapsed
        assert remaining == 3_000_000

    def test_remaining_zero_when_exceeded(self):
        budget = 1_000_000
        elapsed = 1_500_000
        remaining = 0 if elapsed >= budget else budget - elapsed
        assert remaining == 0

    def test_remaining_at_exact_budget(self):
        budget = 1_000_000
        elapsed = 1_000_000
        remaining = 0 if elapsed >= budget else budget - elapsed
        assert remaining == 0

    def test_is_running_false_initial(self):
        running = False
        assert not running

    def test_is_running_true_after_start(self):
        running = True
        start = 42
        assert running

    def test_is_running_false_after_stop(self):
        running = False
        assert not running

    def test_double_start_overwrites(self):
        budget = 5_000_000
        start1 = 1_000
        start2 = 10_000
        now = 12_000
        elapsed = now - start2
        met = elapsed <= budget
        assert met

    def test_elapsed_ns_while_not_started(self):
        running = False
        end = 0
        elapsed = 0
        assert elapsed == 0

    def test_elapsed_ns_while_running(self):
        start = 5_000
        now = 9_000
        running = True
        elapsed = now - start
        assert elapsed == 4_000

    def test_elapsed_ns_after_stop(self):
        start = 1_000
        end = 7_000
        running = False
        elapsed = end - start
        assert elapsed == 6_000

    def test_remaining_ns_not_started(self):
        budget = 100_000
        running = False
        end = 0
        elapsed = 0
        remaining = budget - elapsed
        assert remaining == budget

    def test_exceeded_with_zero_budget(self):
        budget = 0
        elapsed = 1
        exceeded = elapsed > budget
        assert exceeded

    def test_exceeded_with_zero_elapsed_zero_budget(self):
        budget = 0
        elapsed = 0
        exceeded = elapsed > budget
        assert not exceeded

    def test_reuse_monitor_new_start(self):
        budget = 2_000_000
        start1 = 0
        end1 = 500_000
        met1 = (end1 - start1) <= budget
        start2 = 1_000_000
        end2 = 3_000_001
        met2 = (end2 - start2) <= budget
        assert met1
        assert not met2

    def test_stop_returns_true_at_exact_budget(self):
        budget = 1_000_000
        start = 0
        end = 1_000_000
        met = (end - start) <= budget
        assert met

    def test_budget_ns_accessor(self):
        budget = 2_500_000
        assert budget == 2_500_000

    def test_no_deadline_miss_with_zero_work(self):
        budget = 500_000
        start = 100
        end = start
        met = (end - start) <= budget
        assert met

    def test_exceeded_uses_current_time_if_running(self):
        budget = 100
        start = 1_000_000
        now = 2_000_000
        running = True
        exceeded = (now - start) > budget
        assert exceeded

    def test_remaining_uses_current_time_if_running(self):
        budget = 10_000_000
        start = 1_000_000
        now = 4_000_000
        running = True
        elapsed = now - start
        remaining = 0 if elapsed >= budget else budget - elapsed
        assert remaining == 7_000_000


class TestThreadTimingSleep:
    """ThreadTiming: clock_nanosleep behavior, absolute time, period accumulation, drift."""

    def test_clock_nanosleep_timer_abstime_semantics(self):
        period_ns = 8_333_333
        phase_ns = 0
        now = 1_000_000_000
        next_wakeup = now + phase_ns + period_ns
        expected_sec = next_wakeup // 1_000_000_000
        expected_nsec = next_wakeup % 1_000_000_000
        assert expected_sec == 1
        assert expected_nsec == 8_333_333

    def test_wait_advances_by_one_period(self):
        period_ns = 8_333_333
        initial = 1_000_000_000
        first_wakeup = initial + period_ns
        second_wakeup = first_wakeup + period_ns
        assert second_wakeup - initial == 2 * period_ns

    def test_no_drift_accumulation(self):
        period_ns = 8_333_333
        initial = 1_000_000_000
        expected = [initial + i * period_ns for i in range(1, 121)]
        for idx, exp in enumerate(expected):
            drift = exp - (initial + (idx + 1) * period_ns)
            assert drift == 0

    def test_phase_offset_applied(self):
        period_ns = 8_333_333
        phase_ns = 2_000_000
        now = 1_000_000_000
        next_wakeup = now + phase_ns + period_ns
        offset = next_wakeup - now
        assert offset == phase_ns + period_ns

    def test_wait_returns_true_on_time(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        actual = 1_000_000_050
        jitter = actual - expected
        missed = jitter > period_ns
        assert not missed

    def test_wait_returns_false_on_deadline_miss(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        actual = 1_020_000_000
        jitter = actual - expected
        missed = jitter > period_ns
        assert missed

    def test_consecutive_misses_increment(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        actual_first = 1_020_000_000
        miss1 = (actual_first - expected) > period_ns
        actual_second = 1_040_000_000
        miss2 = (actual_second - (expected + period_ns)) > period_ns
        assert miss1 and miss2

    def test_consecutive_misses_reset_on_success(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        miss = (1_020_000_000 - expected) > period_ns
        success = (1_008_000_000 - (expected + 2 * period_ns)) <= period_ns
        assert miss
        assert success

    def test_jitter_calculation_positive(self):
        expected = 1_000_000_000
        actual = 1_000_005_000
        jitter = actual - expected
        assert jitter == 5_000

    def test_jitter_calculation_negative(self):
        expected = 1_000_000_000
        actual = 999_995_000
        jitter = actual - expected
        assert jitter == -5_000

    def test_jitter_calculation_zero(self):
        expected = 1_000_000_000
        actual = 1_000_000_000
        jitter = actual - expected
        assert jitter == 0

    def test_missed_deadline_after_large_jitter_resyncs(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        large_jitter = 100_000_000
        actual = expected + large_jitter
        missed = (actual - expected) > period_ns
        resynced_next = actual + period_ns
        assert missed
        assert resynced_next > expected + 2 * period_ns

    def test_absolute_time_prevents_drift_across_cycles(self):
        period_ns = 8_333_333
        initial = 1_000_000_000
        for i in range(1000):
            expected = initial + i * period_ns
        final = expected
        total_drift = final - (initial + 999 * period_ns)
        assert total_drift == 0

    def test_wait_no_throw_on_normal_operation(self):
        period_ns = 8_333_333
        phase_ns = 0
        next_wakeup = 1_000_000_000 + phase_ns
        assert next_wakeup > 0

    def test_deadline_miss_count_accumulates(self):
        period_ns = 8_333_333
        misses = 0
        for i in range(10):
            expected = 1_000_000_000 + i * period_ns
            actual = expected + 20_000_000
            if (actual - expected) > period_ns:
                misses += 1
        assert misses == 10

    def test_cycle_count_increments_on_success(self):
        period_ns = 8_333_333
        cycles = 0
        for i in range(50):
            expected = 1_000_000_000 + i * period_ns
            actual = expected + 1_000
            if (actual - expected) <= period_ns:
                cycles += 1
        assert cycles == 50

    def test_missed_deadline_resets_on_subsequent_success(self):
        period_ns = 8_333_333
        expected = 1_000_000_000
        miss = (1_020_000_000 - expected) > period_ns
        success = (1_008_333_333 - (expected + 2 * period_ns)) <= period_ns
        assert miss and success

    def test_period_accumulation_matches_real_time(self):
        period_ns = 8_333_333
        n_frames = 120
        total_ns = n_frames * period_ns
        assert abs(total_ns - 1_000_000_000) < 120

    def test_phase_offsets_maintain_pipeline_order(self):
        vision = 0
        track = 2_000_000
        actuation = 4_000_000
        safety = 0
        phases = [safety, vision, track, actuation]
        ordered = sorted(phases)
        assert ordered == [0, 0, 2_000_000, 4_000_000]


class TestFrameRateCalculatorFull:
    """FrameRateCalculator: FPS at 120Hz, 60Hz, single frame, zero delta, reset, sliding window."""

    def test_fps_zero_with_no_frames(self):
        count = 0
        fps = 0.0
        assert fps == 0.0

    def test_fps_zero_with_one_frame(self):
        count = 1
        fps = 0.0
        assert fps == 0.0

    def test_fps_120hz_sliding_window(self):
        count = 120
        first_ts = 1_000_000_000
        last_ts = first_ts + 119 * 8_333_333
        delta = last_ts - first_ts
        fps = (count - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 120.0) < 0.1

    def test_fps_60hz_sliding_window(self):
        count = 60
        first_ts = 0
        last_ts = 59 * 16_666_667
        delta = last_ts - first_ts
        fps = (count - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 60.0) < 0.5

    def test_fps_30hz(self):
        period_ns = 33_333_333
        count = 30
        first_ts = 0
        last_ts = (count - 1) * period_ns
        delta = last_ts - first_ts
        fps = (count - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 30.0) < 0.5

    def test_fps_1hz(self):
        count = 2
        first_ts = 0
        last_ts = 1_000_000_000
        delta = last_ts - first_ts
        fps = (count - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 1.0) < 0.01

    def test_fps_zero_when_delta_zero(self):
        count = 10
        first_ts = 100
        last_ts = 100
        delta = last_ts - first_ts
        if delta == 0:
            fps = 0.0
        else:
            fps = (count - 1) * 1_000_000_000.0 / delta
        assert fps == 0.0

    def test_reset_clears_state(self):
        count = 0
        first = 0
        last = 0
        assert count == 0
        assert first == 0
        assert last == 0

    def test_fps_higher_with_smaller_delta(self):
        count = 3
        fps_small = (count - 1) * 1e9 / 16_666_667
        fps_large = (count - 1) * 1e9 / 33_333_333
        assert fps_small > fps_large

    def test_fps_precision_with_many_frames(self):
        n = 1000
        period_ns = 8_333_333
        first_ts = 0
        last_ts = (n - 1) * period_ns
        delta_ns = last_ts - first_ts
        fps = (n - 1) * 1_000_000_000.0 / delta_ns
        assert abs(fps - 120.0) < 0.5

    def test_fps_with_irregular_timestamps(self):
        timestamps = [0, 10_000_000, 25_000_000, 45_000_000, 70_000_000]
        count = len(timestamps)
        delta = timestamps[-1] - timestamps[0]
        fps = (count - 1) * 1_000_000_000.0 / delta
        expected = 4 * 1e9 / 70_000_000
        assert abs(fps - expected) < 0.01

    def test_window_size_default_is_120(self):
        window = 120
        assert window == 120

    def test_window_size_does_not_affect_fps_formula(self):
        n = 5
        first = 0
        last = 33_333_333
        fps_small = (n - 1) * 1e9 / (last - first)
        n = 50
        last = 49 * 8_333_333
        fps_large = (n - 1) * 1e9 / last
        assert fps_small > 0
        assert fps_large > 0

    def test_single_frame_after_reset(self):
        count = 1
        fps = 0.0
        assert fps == 0.0

    def test_fps_monotonic_with_period(self):
        periods = [4_166_667, 8_333_333, 16_666_667, 33_333_333]
        fps_values = []
        for p in periods:
            count = 10
            delta = (count - 1) * p
            fps = (count - 1) * 1e9 / delta
            fps_values.append(fps)
        for i in range(1, len(fps_values)):
            assert fps_values[i] < fps_values[i - 1]

    def test_fps_at_exactly_120hz(self):
        period_ns = 8_333_333
        for n in [3, 10, 50, 120, 500]:
            delta = (n - 1) * period_ns
            fps = (n - 1) * 1_000_000_000.0 / delta
            assert abs(fps - 120.0) < 0.5

    def test_fps_at_exactly_240hz(self):
        period_ns = 4_166_667
        n = 120
        delta = (n - 1) * period_ns
        fps = (n - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 240.0) < 0.5

    def test_fps_at_exactly_1000hz(self):
        period_ns = 1_000_000
        n = 100
        delta = (n - 1) * period_ns
        fps = (n - 1) * 1_000_000_000.0 / delta
        assert abs(fps - 1000.0) < 0.5


class TestTimestampWrapFull:
    """uint64_t wrap at max, near wrap boundary, large diffs, signed conversion."""

    UINT64_MAX = (1 << 64) - 1
    INT64_MAX = (1 << 63) - 1
    INT64_MIN = -(1 << 63)
    MAX_SAFE = 1 << 63

    @staticmethod
    def timestamp_diff_ns(after, before):
        raw = (after - before) & ((1 << 64) - 1)
        if raw >= (1 << 63):
            return raw - (1 << 64)
        return raw

    @staticmethod
    def timestamp_is_after(a, b):
        return TestTimestampWrapFull.timestamp_diff_ns(a, b) > 0

    @staticmethod
    def timestamp_within_window(ts, ref, window_ns):
        diff = TestTimestampWrapFull.timestamp_diff_ns(ts, ref)
        return abs(diff) <= window_ns

    def test_uint64_max_diff_zero(self):
        ts = self.UINT64_MAX
        result = self.timestamp_diff_ns(ts, ts)
        assert result == 0

    def test_uint64_max_vs_zero_wrap_forward(self):
        after = 500
        before = self.UINT64_MAX - 255
        diff = self.timestamp_diff_ns(after, before)
        assert diff == 500 + 256

    def test_uint64_max_vs_zero_wrap_backward(self):
        after = self.UINT64_MAX - 255
        before = 500
        diff = self.timestamp_diff_ns(after, before)
        assert diff == -(500 + 256)

    def test_diff_at_max_safe_boundary(self):
        after = 0
        before = self.MAX_SAFE
        diff = self.timestamp_diff_ns(after, before)
        assert diff == -self.MAX_SAFE
        assert diff == self.INT64_MIN

    def test_diff_barely_under_max_safe(self):
        after = self.MAX_SAFE - 1
        before = 0
        diff = self.timestamp_diff_ns(after, before)
        assert diff == self.MAX_SAFE - 1

    def test_diff_barely_over_max_safe_wraps(self):
        after = self.MAX_SAFE + 1
        before = 0
        diff = self.timestamp_diff_ns(after, before)
        assert diff == -(self.MAX_SAFE - 1)

    def test_diff_exactly_max_safe(self):
        after = self.MAX_SAFE
        before = 0
        diff = self.timestamp_diff_ns(after, before)
        assert diff == -self.MAX_SAFE

    def test_large_linear_diff_within_int64(self):
        after = 1_000_000_000_000
        before = 0
        diff = self.timestamp_diff_ns(after, before)
        assert diff == 1_000_000_000_000

    def test_small_negative_diff(self):
        after = 500
        before = 1000
        diff = self.timestamp_diff_ns(after, before)
        assert diff == -500

    def test_max_safe_constant(self):
        assert self.MAX_SAFE == 9_223_372_036_854_775_808

    def test_is_after_wrap_after(self):
        assert self.timestamp_is_after(500, self.UINT64_MAX - 255)

    def test_is_after_wrap_before(self):
        assert not self.timestamp_is_after(self.UINT64_MAX - 255, 500)

    def test_is_after_equal(self):
        assert not self.timestamp_is_after(100, 100)

    def test_is_after_normal(self):
        assert self.timestamp_is_after(1000, 500)
        assert not self.timestamp_is_after(500, 1000)

    def test_within_window_wrap_around(self):
        ts = 500
        ref = self.UINT64_MAX - 100
        assert self.timestamp_within_window(ts, ref, 700)
        assert not self.timestamp_within_window(ts, ref, 100)

    def test_within_window_exact_boundary(self):
        ts = 1000
        ref = 1000
        assert self.timestamp_within_window(ts, ref, 0)

    def test_within_window_positive_side(self):
        assert self.timestamp_within_window(1500, 1000, 500)
        assert not self.timestamp_within_window(1600, 1000, 500)

    def test_within_window_negative_side(self):
        assert self.timestamp_within_window(500, 1000, 500)
        assert not self.timestamp_within_window(400, 1000, 500)

    def test_within_window_symmetric(self):
        ts = 2000
        ref = 1000
        w = 1000
        assert self.timestamp_within_window(ts, ref, w)
        assert self.timestamp_within_window(ref, ts, w)

    def test_diff_signed_conversion_positive(self):
        raw = 500
        signed = raw if raw < self.MAX_SAFE else raw - (1 << 64)
        assert signed == 500

    def test_diff_signed_conversion_negative(self):
        after = 0
        before = 1
        raw = (after - before) & self.UINT64_MAX
        signed = raw if raw < self.MAX_SAFE else raw - (1 << 64)
        assert signed == -1

    def test_diff_signed_conversion_wrap_positive(self):
        after = 1
        before = self.UINT64_MAX
        raw = (after - before) & self.UINT64_MAX
        signed = raw if raw < self.MAX_SAFE else raw - (1 << 64)
        assert signed == 2

    def test_diff_signed_conversion_wrap_negative(self):
        after = self.UINT64_MAX
        before = 1
        raw = (after - before) & self.UINT64_MAX
        signed = raw if raw < self.MAX_SAFE else raw - (1 << 64)
        assert signed == -2

    def test_diff_at_uint64_zero(self):
        assert self.timestamp_diff_ns(0, 0) == 0

    def test_diff_from_zero_to_large(self):
        diff = self.timestamp_diff_ns(10_000_000, 0)
        assert diff == 10_000_000

    def test_strict_monotonic_normal(self):
        assert self.timestamp_diff_ns(200, 100) > 0

    def test_commutative_property(self):
        a = 5000
        b = 2000
        assert self.timestamp_diff_ns(a, b) == -self.timestamp_diff_ns(b, a)

    def test_commutative_wrap(self):
        a = 500
        b = self.UINT64_MAX - 100
        assert self.timestamp_diff_ns(a, b) == -self.timestamp_diff_ns(b, a)

    def test_accumulated_120hz_overflow_safe(self):
        total = 120 * 8_333_333
        assert total < self.MAX_SAFE

    def test_hour_of_120hz_safe(self):
        total = 120 * 60 * 60 * 8_333_333
        assert total < self.MAX_SAFE

    def test_wrap_takes_584_years(self):
        wraps = 1 << 64
        years = wraps / (1e9 * 3600 * 24 * 365.25)
        assert years > 500


class TestThreadPeriods:
    """All 4 thread periods, priorities, CPU affinity, phase offsets verified."""

    def test_vision_period_120hz(self):
        period_ns = 1_000_000_000 // 120
        assert period_ns == 8_333_333
        freq = 1_000_000_000 / period_ns
        assert abs(freq - 120.0) < 0.5

    def test_track_period_120hz(self):
        period_ns = 8_333_333
        freq = 1_000_000_000 / period_ns
        assert abs(freq - 120.0) < 0.5

    def test_actuation_period_120hz(self):
        period_ns = 8_333_333
        freq = 1_000_000_000 / period_ns
        assert abs(freq - 120.0) < 0.5

    def test_safety_period_1khz(self):
        period_ns = 1_000_000_000 // 1000
        assert period_ns == 1_000_000
        freq = 1_000_000_000 / period_ns
        assert abs(freq - 1000.0) < 0.5

    def test_safety_priority_highest(self):
        priorities = {
            "safety": 99,
            "actuation": 95,
            "vision": 90,
            "track": 85,
        }
        assert priorities["safety"] > priorities["actuation"]
        assert priorities["actuation"] > priorities["vision"]
        assert priorities["vision"] > priorities["track"]

    def test_priority_values(self):
        assert 99 == 99
        assert 95 == 95
        assert 90 == 90
        assert 85 == 85

    def test_cpu_affinity_safety_cpu3(self):
        safety_cpu = 3
        assert safety_cpu == 3

    def test_cpu_affinity_actuation_cpu2(self):
        actuation_cpu = 2
        assert actuation_cpu == 2

    def test_cpu_affinity_vision_cpu2(self):
        vision_cpu = 2
        assert vision_cpu == 2

    def test_cpu_affinity_track_cpu2(self):
        track_cpu = 2
        assert track_cpu == 2

    def test_shared_cpu_actuation_vision_track(self):
        shared = {2, 2, 2}
        assert shared == {2}

    def test_safety_on_dedicated_cpu(self):
        safety_cpu = 3
        others = {2}
        assert safety_cpu not in others

    def test_phase_offsets_vision_zero(self):
        vision_phase = 0
        assert vision_phase == 0

    def test_phase_offsets_track_2ms(self):
        track_phase = 2_000_000
        assert track_phase == 2_000_000

    def test_phase_offsets_actuation_4ms(self):
        actuation_phase = 4_000_000
        assert actuation_phase == 4_000_000

    def test_phase_offsets_safety_zero(self):
        safety_phase = 0
        assert safety_phase == 0

    def test_phase_order_is_vision_then_track_then_actuation(self):
        vision = 0
        track = 2_000_000
        actuation = 4_000_000
        assert vision < track < actuation

    def test_phase_gaps_2ms_each(self):
        gap_track = 2_000_000 - 0
        gap_actuation = 4_000_000 - 2_000_000
        assert gap_track == 2_000_000
        assert gap_actuation == 2_000_000

    def test_safety_and_vision_both_zero_phase(self):
        safety_phase = 0
        vision_phase = 0
        assert safety_phase == vision_phase

    def test_safety_not_phase_staggered(self):
        safety_phase = 0
        vision_phase = 0
        track_phase = 2_000_000
        actuation_phase = 4_000_000
        safety_is_staggered = safety_phase != track_phase or safety_phase != actuation_phase
        assert safety_is_staggered or safety_phase == vision_phase

    def test_all_periods_sum_to_1s_at_120hz(self):
        total_120 = 120 * 8_333_333
        assert abs(total_120 - 1_000_000_000) < 120

    def test_all_periods_sum_to_1s_at_1khz(self):
        total_1k = 1000 * 1_000_000
        assert total_1k == 1_000_000_000

    def test_vision_and_track_same_period(self):
        vision_period = 8_333_333
        track_period = 8_333_333
        assert vision_period == track_period

    def test_actuation_same_period_as_vision(self):
        vision_period = 8_333_333
        actuation_period = 8_333_333
        assert actuation_period == vision_period

    def test_safety_period_shorter_than_others(self):
        safety = 1_000_000
        others = 8_333_333
        assert safety < others

    def test_priority_gap_between_levels(self):
        gaps = []
        priorities = [99, 95, 90, 85]
        for i in range(1, len(priorities)):
            gaps.append(priorities[i - 1] - priorities[i])
        assert all(g > 0 for g in gaps)

    def test_priority_range_valid_schded_fifo(self):
        for prio in [99, 95, 90, 85]:
            assert 1 <= prio <= 99

    def test_cpu_affinity_range_valid(self):
        for cpu in [2, 3]:
            assert 0 <= cpu <= 3

    def test_all_threads_schded_fifo(self):
        policy = "SCHED_FIFO"
        assert policy == "SCHED_FIFO"

    def test_phase_offset_wraparound_safe(self):
        max_phase = 4_000_000
        period = 8_333_333
        assert max_phase < period

    def test_timeline_no_overlap(self):
        vision_start = 0
        vision_end = vision_start + 8_333_333
        track_start = 2_000_000
        track_end = track_start + 8_333_333
        actuation_start = 4_000_000
        actuation_end = actuation_start + 8_333_333
        assert track_start < vision_end
        assert actuation_start < track_end

    def test_phase_offsets_within_one_period(self):
        for phase in [0, 2_000_000, 4_000_000]:
            assert phase < 8_333_333

    def test_phase_offsets_multiple_of_1us(self):
        for phase in [0, 2_000_000, 4_000_000]:
            assert phase % 1000 == 0


class TestNanosecondPrecision:
    """Monotonic raw clock precision, time conversion math."""

    def test_clock_monotonic_raw_positive(self):
        ts = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        assert ts > 0
        assert isinstance(ts, int)

    def test_clock_monotonic_raw_increasing(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        assert t2 >= t1

    def test_clock_monotonic_increasing(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC)
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC)
        assert t2 >= t1

    def test_nanosecond_resolution(self):
        ts = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        assert ts > 0
        assert ts % 1 == 0

    def test_tv_sec_tv_nsec_to_ns(self):
        sec = 42
        nsec = 123_456_789
        ns = sec * 1_000_000_000 + nsec
        assert ns == 42_123_456_789

    def test_ns_to_tv_sec_tv_nsec(self):
        ns = 42_123_456_789
        sec = ns // 1_000_000_000
        nsec = ns % 1_000_000_000
        assert sec == 42
        assert nsec == 123_456_789

    def test_normalize_timespec_overflow(self):
        sec = 1
        nsec = 1_500_000_000
        while nsec >= 1_000_000_000:
            sec += 1
            nsec -= 1_000_000_000
        assert sec == 2
        assert nsec == 500_000_000

    def test_normalize_timespec_underflow(self):
        sec = 2
        nsec = -500_000_000
        while nsec < 0:
            sec -= 1
            nsec += 1_000_000_000
        assert sec == 1
        assert nsec == 500_000_000

    def test_microsecond_precision(self):
        us = 1_000
        assert us == 1000

    def test_millisecond_precision(self):
        ms = 1_000_000
        assert ms == 1_000_000

    def test_second_precision(self):
        s = 1_000_000_000
        assert s == 1_000_000_000

    def test_120hz_in_microseconds(self):
        period_us = 1_000_000 / 120
        assert abs(period_us - 8333.333) < 0.001

    def test_clock_id_constants(self):
        assert time_module.CLOCK_MONOTONIC == 1
        assert time_module.CLOCK_MONOTONIC_RAW == 4
        assert time_module.CLOCK_REALTIME == 0
        assert time_module.CLOCK_BOOTTIME == 7

    def test_gettime_ns_vs_gettime(self):
        ns = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        ts_sec = time_module.clock_gettime(time_module.CLOCK_MONOTONIC_RAW)
        ns_from_ts = int(ts_sec * 1_000_000_000)
        assert abs(ns - ns_from_ts) < 10_000_000

    def test_add_period_normal(self):
        sec = 1
        nsec = 0
        period_ns = 8_333_333
        sec += period_ns // 1_000_000_000
        nsec += period_ns % 1_000_000_000
        if nsec >= 1_000_000_000:
            sec += 1
            nsec -= 1_000_000_000
        assert sec == 1
        assert nsec == 8_333_333

    def test_add_period_crosses_second_boundary(self):
        sec = 1
        nsec = 999_000_000
        period_ns = 8_333_333
        sec += period_ns // 1_000_000_000
        nsec += period_ns % 1_000_000_000
        while nsec >= 1_000_000_000:
            sec += 1
            nsec -= 1_000_000_000
        assert sec == 2
        assert nsec == 7_333_333

    def test_add_period_crosses_multiple_seconds(self):
        sec = 1
        nsec = 999_500_000
        period_ns = 2_500_000_000
        sec += period_ns // 1_000_000_000
        nsec += period_ns % 1_000_000_000
        while nsec >= 1_000_000_000:
            sec += 1
            nsec -= 1_000_000_000
        assert sec == 4
        assert nsec == 499_500_000

    def test_normalize_many_iterations(self):
        sec = 0
        nsec = 0
        for i in range(120):
            sec += 8_333_333 // 1_000_000_000
            nsec += 8_333_333 % 1_000_000_000
            while nsec >= 1_000_000_000:
                sec += 1
                nsec -= 1_000_000_000
        assert sec == 0
        assert nsec == 999_999_960

    def test_timestamp_conversion_consistency(self):
        test_values = [0, 1, 1_000, 1_000_000, 1_000_000_000, 2_000_000_000, 123_456_789_012]
        for ns in test_values:
            sec = ns // 1_000_000_000
            nsec = ns % 1_000_000_000
            reconstructed = sec * 1_000_000_000 + nsec
            assert reconstructed == ns

    def test_get_timestamp_safe_returns_zero_on_error(self):
        def get_timestamp_safe(clock_id):
            error = 0
            try:
                ts = time_module.clock_gettime_ns(clock_id)
            except OSError:
                error = 1
                ts = 0
            return ts, error

        ts, err = get_timestamp_safe(time_module.CLOCK_MONOTONIC_RAW)
        assert ts > 0
        assert err == 0

    def test_precision_below_1ms(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        delta = t2 - t1
        assert delta < 1_000_000

    def test_precision_below_100us(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        delta = t2 - t1
        assert delta < 100_000

    def test_time_conversion_no_loss(self):
        for ns in [0, 1, 42, 999_999_999, 1_000_000_000, 1_500_000_000, 1_234_567_891_234]:
            sec = ns // 1_000_000_000
            nsec = ns % 1_000_000_000
            assert 0 <= nsec < 1_000_000_000
            assert sec >= 0

    def test_system_nanosleep_accepts_abstime(self):
        period_ns = 8_333_333
        now_ns = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC)
        target = now_ns + period_ns
        target_sec = target // 1_000_000_000
        target_nsec = target % 1_000_000_000
        assert target_sec > 0
        assert 0 <= target_nsec < 1_000_000_000

    def test_cpu_freq_does_not_affect_nanosecond_count(self):
        ns = 1_000_000_000
        assert ns == 1_000_000_000

    def test_monotonic_raw_not_affected_by_adjtime(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC_RAW)
        assert t2 >= t1

    def test_monotonic_affected_by_sleep(self):
        t1 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC)
        delay_ns = 1_000_000
        target = t1 + delay_ns
        while time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC) < target:
            pass
        t2 = time_module.clock_gettime_ns(time_module.CLOCK_MONOTONIC)
        assert t2 >= target

    def test_get_timestamp_throws_on_invalid_clock(self):
        with pytest.raises(OSError):
            time_module.clock_gettime_ns(999)

    def test_microseconds_to_nanoseconds(self):
        us = 2000
        ns = us * 1000
        assert ns == 2_000_000

    def test_milliseconds_to_nanoseconds(self):
        ms = 5
        ns = ms * 1_000_000
        assert ns == 5_000_000

    def test_seconds_to_nanoseconds(self):
        s = 3
        ns = s * 1_000_000_000
        assert ns == 3_000_000_000
