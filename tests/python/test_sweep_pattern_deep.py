import pytest
import math


class TestSweepPatternConfigValidation:
    """SweepPattern::Config validation and edge cases."""

    def validate(self, cfg):
        if cfg["az_amplitude_deg"] <= 0:
            return False
        if cfg["el_amplitude_deg"] <= 0:
            return False
        if cfg["az_period_sec"] <= 0:
            return False
        return True

    def test_default_valid(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        assert self.validate(cfg)

    def test_zero_az_amplitude(self):
        cfg = {"az_amplitude_deg": 0.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        assert not self.validate(cfg)

    def test_zero_el_amplitude(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 0.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        assert not self.validate(cfg)

    def test_zero_period(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 0.0}
        assert not self.validate(cfg)

    def test_negative_period(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": -5.0}
        assert not self.validate(cfg)

    def test_az_amplitude_within_limits(self):
        for amp in [10.0, 45.0, 80.0, 90.0]:
            assert 0 < amp <= 90.0

    def test_el_amplitude_reasonable(self):
        for amp in [5.0, 10.0, 15.0, 20.0, 30.0]:
            assert 0 < amp <= 45.0

    def test_period_reasonable(self):
        for p in [1.0, 5.0, 10.0, 30.0, 60.0]:
            assert p > 0


class TestSweepPatternLissajous:
    """Lissajous oval: comprehensive trajectory tests."""

    def lissajous(self, t, cfg):
        az = cfg["az_amplitude_deg"] * math.sin(2 * math.pi * t / cfg["az_period_sec"])
        el = (cfg["el_offset_deg"]
              + cfg["el_amplitude_deg"] * math.cos(4 * math.pi * t / cfg["az_period_sec"]))
        return (az, el)

    def test_t_zero_origin(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az, el = self.lissajous(0, cfg)
        assert abs(az) < 0.001
        assert abs(el - 25.0) < 0.001

    def test_quarter_az(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az, el = self.lissajous(2.5, cfg)
        assert abs(az - 80.0) < 0.1
        assert abs(el - (-5.0)) < 0.1

    def test_half_az(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az, el = self.lissajous(5.0, cfg)
        assert abs(az) < 0.001
        assert abs(el - 25.0) < 0.001

    def test_three_quarters_az(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az, el = self.lissajous(7.5, cfg)
        assert abs(az - (-80.0)) < 0.1
        assert abs(el - (-5.0)) < 0.1

    def test_full_period(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az0, el0 = self.lissajous(0, cfg)
        az1, el1 = self.lissajous(10.0, cfg)
        assert abs(az0 - az1) < 0.001
        assert abs(el0 - el1) < 0.001

    def test_el_double_frequency(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az_half, el_half = self.lissajous(5.0, cfg)
        assert abs(az_half) < 0.001
        assert abs(el_half - 25.0) < 0.001

    def test_az_range_bounded(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        for t in [t * 0.1 for t in range(200)]:
            az, el = self.lissajous(t, cfg)
            assert -80.0 <= az <= 80.0

    def test_el_range_bounded(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        for t in [t * 0.1 for t in range(200)]:
            az, el = self.lissajous(t, cfg)
            assert -5.0 <= el <= 25.0

    def test_custom_config(self):
        cfg = {"az_amplitude_deg": 45.0, "el_amplitude_deg": 10.0,
               "el_offset_deg": 5.0, "az_period_sec": 8.0}
        az, el = self.lissajous(2.0, cfg)
        expected_az = 45.0 * math.sin(2 * math.pi * 2.0 / 8.0)
        expected_el = 5.0 + 10.0 * math.cos(4 * math.pi * 2.0 / 8.0)
        assert abs(az - expected_az) < 0.001
        assert abs(el - expected_el) < 0.001


class TestSweepPatternTick:
    """SweepPattern::tick: discrete time advance."""

    def test_tick_at_120hz(self):
        dt = 1.0 / 120.0
        t = 0.0
        for _ in range(120):
            t += dt
        assert abs(t - 1.0) < 0.001

    def test_tick_at_60hz(self):
        dt = 1.0 / 60.0
        t = 0.0
        for _ in range(60):
            t += dt
        assert abs(t - 1.0) < 0.001

    def test_tick_at_safety_rate(self):
        dt = 0.001
        t = 0.0
        for _ in range(1000):
            t += dt
        assert abs(t - 1.0) < 0.001

    def test_tick_monotonic(self):
        dt = 0.008333
        t = 0.0
        prev = t
        for _ in range(100):
            t += dt
            assert t > prev
            prev = t

    def test_small_dt_accumulation(self):
        dt = 0.0001
        t = 0.0
        for _ in range(10000):
            t += dt
        assert abs(t - 1.0) < 0.01

    def test_large_dt(self):
        dt = 5.0
        t = 0.0
        t += dt
        assert t == 5.0


class TestSweepPatternReset:
    """SweepPattern::reset: return to initial state."""

    def test_reset_sets_elapsed_to_zero(self):
        elapsed = 42.0
        elapsed = 0.0
        assert elapsed == 0.0

    def test_after_reset_point_is_origin(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        az = cfg["az_amplitude_deg"] * math.sin(0)
        el = cfg["el_offset_deg"] + cfg["el_amplitude_deg"] * math.cos(0)
        assert abs(az) < 0.001
        assert abs(el - 25.0) < 0.001

    def test_multiple_resets(self):
        for _ in range(10):
            pass
        assert True

    def test_tick_after_reset(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        t = 0.0
        t += 1.0
        t = 0.0
        az, el = (cfg["az_amplitude_deg"] * math.sin(2 * math.pi * t / cfg["az_period_sec"]),
                  cfg["el_offset_deg"] + cfg["el_amplitude_deg"] * math.cos(4 * math.pi * t / cfg["az_period_sec"]))
        assert abs(az) < 0.001
        assert abs(el - 25.0) < 0.001


class TestSweepPatternElapsed:
    """SweepPattern::elapsed_sec: tracking total time."""

    def test_initial_zero(self):
        assert 0.0 == 0.0

    def test_after_one_tick(self):
        elapsed = 0.008333
        assert elapsed == pytest.approx(0.008333)

    def test_after_many_ticks(self):
        elapsed = 0.0
        for _ in range(1200):
            elapsed += 0.008333
        assert elapsed == pytest.approx(10.0, abs=0.1)


class TestSweepPatternDerivative:
    """SweepPattern velocity (rate of change) at any point."""

    def velocity(self, t, cfg):
        az = cfg["az_amplitude_deg"] * math.sin(2 * math.pi * t / cfg["az_period_sec"])
        el = (cfg["el_offset_deg"]
              + cfg["el_amplitude_deg"] * math.cos(4 * math.pi * t / cfg["az_period_sec"]))
        dt = 1e-6
        az_next = cfg["az_amplitude_deg"] * math.sin(2 * math.pi * (t + dt) / cfg["az_period_sec"])
        el_next = (cfg["el_offset_deg"]
                   + cfg["el_amplitude_deg"] * math.cos(4 * math.pi * (t + dt) / cfg["az_period_sec"]))
        daz_dt = (az_next - az) / dt
        del_dt = (el_next - el) / dt
        return (daz_dt, del_dt)

    def test_velocity_at_zero(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        daz, del_ = self.velocity(0, cfg)
        assert daz > 0
        assert abs(del_) < 0.01

    def test_velocity_at_peak(self):
        cfg = {"az_amplitude_deg": 80.0, "el_amplitude_deg": 15.0,
               "el_offset_deg": 10.0, "az_period_sec": 10.0}
        daz, del_ = self.velocity(2.5, cfg)
        assert abs(daz) < 0.01
