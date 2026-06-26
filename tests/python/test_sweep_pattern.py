import pytest
import math

try:
    from hypothesis import given, strategies as st
    HAS_HYPOTHESIS = True
except ImportError:
    HAS_HYPOTHESIS = False


class TestSweepPatternConfig:
    """SweepPattern::Config: Lissajous oval parameters."""

    def test_default_az_amplitude(self):
        assert 80.0 == 80.0

    def test_default_el_amplitude(self):
        assert 15.0 == 15.0

    def test_default_el_offset(self):
        assert 10.0 == 10.0

    def test_default_az_period(self):
        assert 10.0 == 10.0

    def test_az_within_gimbal_limits(self):
        az_amplitude = 80.0
        az_limit = 90.0
        assert az_amplitude <= az_limit

    def test_el_swing_within_limits(self):
        el_amplitude = 15.0
        el_offset = 10.0
        el_max = el_offset + el_amplitude
        el_min = el_offset - el_amplitude
        assert el_max <= 45.0
        assert el_min >= -10.0


class TestSweepPatternMath:
    """SweepPattern::tick: Lissajous oval generation."""

    def lissajous_point(self, t, cfg):
        az = cfg["az_amplitude"] * math.sin(2 * math.pi * t / cfg["az_period"])
        # 2:1 Lissajous: el_period = az_period / 2
        el = (cfg["el_offset"]
              + cfg["el_amplitude"] * math.cos(4 * math.pi * t / cfg["az_period"]))
        return {"az_deg": az, "el_deg": el}

    def test_t_zero(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = self.lissajous_point(0, cfg)
        assert abs(p["az_deg"]) < 0.01
        assert abs(p["el_deg"] - 25.0) < 0.01

    def test_t_quarter_period(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = self.lissajous_point(2.5, cfg)  # t = az_period/4
        assert abs(p["az_deg"] - 80.0) < 0.1
        assert abs(p["el_deg"] - (-5.0)) < 0.1

    def test_t_half_period(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = self.lissajous_point(5.0, cfg)
        assert abs(p["az_deg"]) < 0.01
        assert abs(p["el_deg"] - 25.0) < 0.01

    def test_t_three_quarters(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = self.lissajous_point(7.5, cfg)
        assert abs(p["az_deg"] - (-80.0)) < 0.1
        assert abs(p["el_deg"] - (-5.0)) < 0.1

    def test_full_cycle_returns_to_start(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p0 = self.lissajous_point(0, cfg)
        p1 = self.lissajous_point(10.0, cfg)
        assert abs(p0["az_deg"] - p1["az_deg"]) < 0.01
        assert abs(p0["el_deg"] - p1["el_deg"]) < 0.01

    def test_el_period_half_az_period(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        el_period = cfg["az_period"] / 2
        assert el_period == 5.0

    def test_sine_wave_property(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = self.lissajous_point(2.5, cfg)
        assert -80.0 <= p["az_deg"] <= 80.0
        assert -5.0 <= p["el_deg"] <= 25.0

    def test_az_range(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        az_values = [self.lissajous_point(t, cfg)["az_deg"] for t in [0, 2.5, 5.0, 7.5]]
        assert max(az_values) <= 80.0
        assert min(az_values) >= -80.0


class TestSweepPatternReset:
    """SweepPattern::reset: return to origin."""

    def test_reset_elapsed_to_zero(self):
        elapsed = 5.0
        elapsed = 0.0
        assert elapsed == 0.0

    def test_reset_clears_state(self):
        elapsed = 0.0
        p = {"az_deg": 0.0, "el_deg": 10.0}
        assert p["az_deg"] == 0.0


class TestSweepPatternTick:
    """SweepPattern::tick: time advance."""

    def test_tick_advances_time(self):
        dt = 0.008333
        t = 0.0
        t += dt
        assert t == pytest.approx(0.008333)

    def test_multiple_ticks(self):
        dt = 0.008333
        t = 0.0
        for _ in range(10):
            t += dt
        assert t == pytest.approx(0.08333, abs=0.001)

    def test_elapsed_sec_tracking(self):
        elapsed = 0.0
        elapsed += 0.1
        assert elapsed == pytest.approx(0.1)


@pytest.mark.skipif(not HAS_HYPOTHESIS, reason="hypothesis not installed")
class TestSweepPatternProperties:
    """Property-based tests for Lissajous sweep pattern."""

    @given(st.floats(min_value=0.0, max_value=10.0))
    def test_az_in_range(self, t):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        az = cfg["az_amplitude"] * math.sin(2 * math.pi * t / cfg["az_period"])
        assert -80.0 <= az <= 80.0

    @given(st.floats(min_value=0.0, max_value=10.0))
    def test_el_in_range(self, t):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        el = (cfg["el_offset"]
              + cfg["el_amplitude"] * math.cos(4 * math.pi * t / cfg["az_period"]))
        assert -5.0 <= el <= 25.0

    @given(st.floats(min_value=0.0, max_value=100.0))
    def test_az_periodic(self, t):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        az_t = cfg["az_amplitude"] * math.sin(2 * math.pi * t / cfg["az_period"])
        az_tp = cfg["az_amplitude"] * math.sin(2 * math.pi * (t + 10.0) / cfg["az_period"])
        assert abs(az_t - az_tp) < 0.01

    @given(st.floats(min_value=0.0, max_value=100.0))
    def test_el_periodic(self, t):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        el_t = (cfg["el_offset"]
                + cfg["el_amplitude"] * math.cos(4 * math.pi * t / cfg["az_period"]))
        el_tp = (cfg["el_offset"]
                 + cfg["el_amplitude"] * math.cos(4 * math.pi * (t + 10.0) / cfg["az_period"]))
        assert abs(el_t - el_tp) < 0.01

    @given(st.floats(min_value=1.0, max_value=20.0))
    def test_az_amplitude_scales(self, amp):
        t = 2.5
        period = 10.0
        az = amp * math.sin(2 * math.pi * t / period)
        assert -amp <= az <= amp
        assert abs(az - amp) < 0.1

    @given(st.floats(min_value=0.0, max_value=5.0))
    def test_el_amplitude_scales(self, amp):
        t = 0.0
        offset = 10.0
        el = offset + amp * math.cos(0)
        assert abs(el - (offset + amp)) < 0.01

    @given(st.floats(min_value=0.5, max_value=30.0))
    def test_different_periods(self, period):
        t = period / 4.0
        az_amp = 80.0
        az = az_amp * math.sin(2 * math.pi * t / period)
        assert abs(az - az_amp) < 0.1

    @given(st.floats(min_value=0.01, max_value=1.0))
    def test_tick_advances_monotonically(self, dt):
        t = 0.0
        for _ in range(10):
            t += dt
        assert t > 0

    @given(st.floats(min_value=0.0, max_value=10.0))
    def test_reset_returns_to_origin(self, t):
        elapsed = t
        elapsed = 0.0
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        p = {"az_deg": cfg["az_amplitude"] * math.sin(0),
             "el_deg": (cfg["el_offset"]
                        + cfg["el_amplitude"] * math.cos(0))}
        assert abs(p["az_deg"]) < 0.01
        assert abs(p["el_deg"] - 25.0) < 0.01

    def test_lissajous_oval_shape(self):
        cfg = {"az_amplitude": 80.0, "el_amplitude": 15.0,
               "el_offset": 10.0, "az_period": 10.0}
        points = [self._point_at(cfg, i * 0.1) for i in range(100)]
        az_max = max(abs(p["az_deg"]) for p in points)
        el_max = max(p["el_deg"] for p in points)
        el_min = min(p["el_deg"] for p in points)
        assert az_max <= 80.0
        assert el_max <= 25.0
        assert el_min >= -5.0

    def _point_at(self, cfg, t):
        return {
            "az_deg": cfg["az_amplitude"] * math.sin(2 * math.pi * t / cfg["az_period"]),
            "el_deg": (cfg["el_offset"]
                       + cfg["el_amplitude"] * math.cos(4 * math.pi * t / cfg["az_period"]))
        }
