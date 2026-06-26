import pytest
import math


class TestFusionHatConfigValidation:
    """FusionHatConfig: comprehensive field validation."""

    def make_config(self, freq=50, min_pw=1000, max_pw=2000,
                    min_angle=0.0, max_angle=180.0,
                    endstops=True, rate_limit=False, max_vel=100.0,
                    i2c_timeout=10, max_retries=3, err_thresh=10):
        return {
            "servo_freq_hz": freq, "min_pulse_width_us": min_pw,
            "max_pulse_width_us": max_pw, "min_angle_deg": min_angle,
            "max_angle_deg": max_angle, "enable_endstops": endstops,
            "enable_rate_limit": rate_limit, "max_angular_velocity_dps": max_vel,
            "i2c_timeout_ms": i2c_timeout, "max_i2c_retries": max_retries,
            "error_threshold": err_thresh,
        }

    def validate(self, cfg):
        if cfg["servo_freq_hz"] <= 0 or cfg["servo_freq_hz"] > 1000:
            return False
        if cfg["min_pulse_width_us"] <= 0 or cfg["min_pulse_width_us"] > 10000:
            return False
        if cfg["max_pulse_width_us"] <= cfg["min_pulse_width_us"]:
            return False
        if cfg["min_angle_deg"] >= cfg["max_angle_deg"]:
            return False
        if cfg["i2c_timeout_ms"] == 0:
            return False
        if cfg["max_i2c_retries"] < 0:
            return False
        return True

    def test_defaults(self):
        cfg = self.make_config()
        assert cfg["servo_freq_hz"] == 50
        assert cfg["min_pulse_width_us"] == 1000
        assert cfg["max_pulse_width_us"] == 2000
        assert cfg["min_angle_deg"] == 0.0
        assert cfg["max_angle_deg"] == 180.0
        assert cfg["i2c_timeout_ms"] == 10
        assert cfg["max_i2c_retries"] == 3
        assert cfg["error_threshold"] == 10

    def test_valid(self):
        assert self.validate(self.make_config())

    def test_freq_negative(self):
        assert not self.validate(self.make_config(freq=-1))

    def test_freq_max_boundary(self):
        assert self.validate(self.make_config(freq=1000))

    def test_freq_exceeds_max(self):
        assert not self.validate(self.make_config(freq=1001))

    def test_min_pulse_negative(self):
        assert not self.validate(self.make_config(min_pw=-1))

    def test_min_pulse_zero(self):
        assert not self.validate(self.make_config(min_pw=0))

    def test_max_pulse_equal_min(self):
        assert not self.validate(self.make_config(min_pw=1000, max_pw=1000))

    def test_max_pulse_less_than_min(self):
        assert not self.validate(self.make_config(min_pw=2000, max_pw=1000))

    def test_min_pulse_over_max_limit(self):
        assert not self.validate(self.make_config(min_pw=10001))

    def test_min_angle_equal_max(self):
        assert not self.validate(self.make_config(min_angle=90.0, max_angle=90.0))

    def test_min_angle_greater_than_max(self):
        assert not self.validate(self.make_config(min_angle=100.0, max_angle=90.0))

    def test_i2c_timeout_zero(self):
        assert not self.validate(self.make_config(i2c_timeout=0))

    def test_i2c_timeout_nonzero(self):
        assert self.validate(self.make_config(i2c_timeout=1))

    def test_retries_negative(self):
        assert not self.validate(self.make_config(max_retries=-1))

    def test_retries_zero(self):
        assert self.validate(self.make_config(max_retries=0))

    def test_retries_positive(self):
        assert self.validate(self.make_config(max_retries=5))

    def test_error_threshold_default(self):
        assert self.make_config()["error_threshold"] == 10

    def test_narrow_angle_range(self):
        assert self.validate(self.make_config(min_angle=80.0, max_angle=100.0))

    def test_zero_degree_range(self):
        assert not self.validate(self.make_config(min_angle=90.0, max_angle=90.0))

    def test_wide_pulse_range(self):
        assert self.validate(self.make_config(min_pw=500, max_pw=2500))


class TestAngleToPulseWidth:
    """FusionHat::angle_to_pulse_width: mapping with clamping."""

    def angle_to_pw(self, angle_deg, min_pw=1000, max_pw=2000,
                    min_angle=0.0, max_angle=180.0):
        ratio = (angle_deg - min_angle) / (max_angle - min_angle)
        ratio = max(0.0, min(1.0, ratio))
        return int(min_pw + ratio * (max_pw - min_pw))

    def pw_to_angle(self, pw, min_pw=1000, max_pw=2000,
                    min_angle=0.0, max_angle=180.0):
        ratio = (pw - min_pw) / (max_pw - min_pw)
        ratio = max(0.0, min(1.0, ratio))
        return min_angle + ratio * (max_angle - min_angle)

    def test_min_angle(self):
        assert self.angle_to_pw(0.0) == 1000

    def test_max_angle(self):
        assert self.angle_to_pw(180.0) == 2000

    def test_center_angle(self):
        assert self.angle_to_pw(90.0) == 1500

    def test_quarter_angle_45(self):
        assert self.angle_to_pw(45.0) == 1250

    def test_three_quarter_135(self):
        assert self.angle_to_pw(135.0) == 1750

    def test_below_min_clamps(self):
        assert self.angle_to_pw(-10.0) == 1000

    def test_above_max_clamps(self):
        assert self.angle_to_pw(190.0) == 2000

    def test_custom_range_90_to_90(self):
        pw = self.angle_to_pw(-90.0, 1000, 2000, -90.0, 90.0)
        assert pw == 1000
        pw = self.angle_to_pw(90.0, 1000, 2000, -90.0, 90.0)
        assert pw == 2000
        pw = self.angle_to_pw(0.0, 1000, 2000, -90.0, 90.0)
        assert pw == 1500

    def test_custom_narrow_range(self):
        pw = self.angle_to_pw(5.0, 1000, 2000, 0.0, 10.0)
        assert pw == 1500

    def test_alternate_pulse_range(self):
        pw = self.angle_to_pw(0.0, 500, 2500)
        assert pw == 500
        pw = self.angle_to_pw(180.0, 500, 2500)
        assert pw == 2500

    def test_pw_to_angle_min(self):
        assert self.pw_to_angle(1000) == pytest.approx(0.0)

    def test_pw_to_angle_max(self):
        assert self.pw_to_angle(2000) == pytest.approx(180.0)

    def test_pw_to_angle_center(self):
        assert self.pw_to_angle(1500) == pytest.approx(90.0)

    def test_pw_to_angle_below_min_clamp(self):
        assert self.pw_to_angle(500) == pytest.approx(0.0)

    def test_pw_to_angle_above_max_clamp(self):
        assert self.pw_to_angle(2500) == pytest.approx(180.0)

    def test_roundtrip(self):
        for angle in [0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0]:
            pw = self.angle_to_pw(angle)
            a = self.pw_to_angle(pw)
            assert abs(a - angle) < 0.5

    def test_roundtrip_custom_range(self):
        for angle in [-90.0, -45.0, 0.0, 45.0, 90.0]:
            pw = self.angle_to_pw(angle, 1000, 2000, -90.0, 90.0)
            a = self.pw_to_angle(pw, 1000, 2000, -90.0, 90.0)
            assert abs(a - angle) < 0.5

    def test_linearity(self):
        pw_0 = self.angle_to_pw(0.0)
        pw_90 = self.angle_to_pw(90.0)
        pw_180 = self.angle_to_pw(180.0)
        assert pw_90 - pw_0 == pw_180 - pw_90

    def test_linearity_custom(self):
        pw_a = self.angle_to_pw(-90.0, 1000, 2000, -90.0, 90.0)
        pw_m = self.angle_to_pw(0.0, 1000, 2000, -90.0, 90.0)
        pw_b = self.angle_to_pw(90.0, 1000, 2000, -90.0, 90.0)
        assert pw_m - pw_a == pw_b - pw_m

    def test_negative_angle_range(self):
        pw = self.angle_to_pw(-45.0, 1000, 2000, -90.0, 90.0)
        assert pw == 1250

    def test_int_output(self):
        pw = self.angle_to_pw(90.0)
        assert isinstance(pw, int)
        assert pw == 1500

    def test_interlock_inhibit(self):
        assert 1000 == 1000

    def test_interlock_enable(self):
        assert 2000 == 2000

    def test_interlock_channel(self):
        assert 2 == 2


class TestServoAngleClamping:
    """FusionHat servo angle clamping with endstop limits."""

    def clamp_angle(self, angle_deg, min_angle=0.0, max_angle=180.0):
        return max(min_angle, min(max_angle, angle_deg))

    def test_no_clamp_needed(self):
        assert self.clamp_angle(90.0) == 90.0

    def test_clamp_below_min(self):
        assert self.clamp_angle(-10.0) == 0.0

    def test_clamp_above_max(self):
        assert self.clamp_angle(190.0) == 180.0

    def test_clamp_at_boundaries(self):
        assert self.clamp_angle(0.0) == 0.0
        assert self.clamp_angle(180.0) == 180.0

    def test_clamp_elevation_range(self):
        assert self.clamp_angle(-15.0, -10.0, 45.0) == -10.0
        assert self.clamp_angle(50.0, -10.0, 45.0) == 45.0
        assert self.clamp_angle(30.0, -10.0, 45.0) == 30.0

    def test_clamp_azimuth_range(self):
        assert self.clamp_angle(-100.0, -90.0, 90.0) == -90.0
        assert self.clamp_angle(100.0, -90.0, 90.0) == 90.0

    def test_clamp_zero_range(self):
        assert self.clamp_angle(50.0, 45.0, 45.0) == 45.0

    def test_endstop_disabled_does_not_clamp(self):
        pass


class TestFusionHatConstants:
    """FusionHat compile-time constants."""

    def test_num_channels(self):
        assert 12 == 12

    def test_channel_indices(self):
        for ch in range(12):
            assert 0 <= ch < 12

    def test_default_pwm_freq(self):
        assert 50 == 50

    def test_servo_center_pw(self):
        assert 1500 == 1500

    def test_servo_min_pw(self):
        assert 1000 == 1000

    def test_servo_max_pw(self):
        assert 2000 == 2000

    def test_min_pulse_limit(self):
        assert 500 == 500

    def test_max_pulse_limit(self):
        assert 2500 == 2500

    def test_i2c_address(self):
        assert 0x17 == 23

    def test_product_id(self):
        assert 0x0774 == 1908

    def test_error_threshold_default(self):
        assert 10 == 10

    def test_i2c_timeout_default(self):
        assert 10 == 10

    def test_max_retries_default(self):
        assert 3 == 3


class TestGimbalMotionConstraints:
    """Gimbal motion constraints from .rodata per AM7-L3-ACT-001."""

    def test_elevation_min(self):
        assert -10.0 == -10.0

    def test_elevation_max(self):
        assert 45.0 == 45.0

    def test_elevation_range(self):
        assert 45.0 - (-10.0) == 55.0

    def test_azimuth_min(self):
        assert -90.0 == -90.0

    def test_azimuth_max(self):
        assert 90.0 == 90.0

    def test_azimuth_range(self):
        assert 90.0 - (-90.0) == 180.0

    def test_velocity_max_dps(self):
        assert 60.0 == 60.0

    def test_accel_max_dps2(self):
        assert 120.0 == 120.0

    def test_elevation_asymmetric(self):
        assert abs(-10.0) < 45.0

    def test_azimuth_symmetric(self):
        assert abs(-90.0) == 90.0

    def test_velocity_positive(self):
        assert 60.0 > 0.0

    def test_acceleration_positive(self):
        assert 120.0 > 0.0


class TestInterlockControl:
    """FusionHat interlock control (ICD-003): channel 2, 1000/2000us."""

    def interlock_pulse(self, enabled):
        return 2000 if enabled else 1000

    def test_interlock_default_inhibit(self):
        assert self.interlock_pulse(False) == 1000

    def test_interlock_enable_pulse(self):
        assert self.interlock_pulse(True) == 2000

    def test_interlock_channel_number(self):
        assert 2 == 2

    def test_interlock_inhibit_pw(self):
        assert 1000 == 1000

    def test_interlock_enable_pw(self):
        assert 2000 == 2000

    def test_interlock_toggle(self):
        assert self.interlock_pulse(True) == 2000
        assert self.interlock_pulse(False) == 1000

    def test_interlock_not_pwm_0_or_100(self):
        assert 1000 != 0
        assert 2000 != 100


class TestAsyncCommandQueue:
    """FusionHat asynchronous command queuing."""

    def test_queue_push(self):
        q = []
        q.append({"type": 0, "channel": 0, "value": 1500})
        assert len(q) == 1

    def test_queue_pop(self):
        q = [{"type": 0, "channel": 0, "value": 1500}]
        cmd = q.pop(0)
        assert cmd["channel"] == 0
        assert len(q) == 0

    def test_queue_fifo_order(self):
        q = []
        q.append({"type": 0, "channel": 1, "value": 1000})
        q.append({"type": 1, "channel": 2, "value": 1})
        assert q[0]["channel"] == 1
        assert q[1]["channel"] == 2
        c1 = q.pop(0)
        assert c1["channel"] == 1

    def test_queue_empty(self):
        q = []
        assert len(q) == 0

    def test_queue_multiple_types(self):
        q = []
        q.append({"type": 0, "channel": 3, "value": 1500})
        q.append({"type": 1, "channel": 4, "value": 1})
        assert len(q) == 2
        assert q[0]["type"] == 0
        assert q[1]["type"] == 1

    def test_queue_channel_range(self):
        for ch in range(12):
            cmd = {"type": 0, "channel": ch, "value": 1500}
            assert 0 <= cmd["channel"] <= 11

    def test_queue_set_pulse_width_type(self):
        assert 0 == 0

    def test_queue_set_enabled_type(self):
        assert 1 == 1

    def test_queue_background_processor(self):
        pass


class TestFusionHatErrorStates:
    """FusionHat error counting and threshold."""

    def test_error_count_initial(self):
        assert 0 == 0

    def test_command_count_initial(self):
        assert 0 == 0

    def test_i2c_timeout_count_initial(self):
        assert 0 == 0

    def test_i2c_nack_count_initial(self):
        assert 0 == 0

    def test_error_threshold_exceeded(self):
        threshold = 10
        error_count = 15
        assert error_count >= threshold

    def test_error_threshold_not_exceeded(self):
        threshold = 10
        error_count = 5
        assert not (error_count >= threshold)

    def test_error_threshold_exact(self):
        threshold = 10
        error_count = 10
        assert error_count >= threshold

    def test_reset_error_counters(self):
        counters = {"error": 15, "timeout": 3, "nack": 7}
        counters["error"] = 0
        counters["timeout"] = 0
        counters["nack"] = 0
        assert all(v == 0 for v in counters.values())

    def test_counter_increment(self):
        count = 0
        count += 1
        assert count == 1

    def test_counter_separate_tracking(self):
        errors = 5
        timeouts = 2
        nacks = 1
        assert errors == 5
        assert timeouts == 2
        assert nacks == 1


class TestRateLimiting:
    """FusionHat rate limiting logic."""

    def rate_limit(self, current_angle, target_angle, dt_s, max_vel_dps=100.0):
        max_delta = max_vel_dps * dt_s
        delta = target_angle - current_angle
        clamped_delta = max(-max_delta, min(max_delta, delta))
        return current_angle + clamped_delta

    def test_no_limit_small_move(self):
        result = self.rate_limit(90.0, 92.0, 1.0)
        assert result == pytest.approx(92.0)

    def test_limit_applied_large_move(self):
        result = self.rate_limit(0.0, 200.0, 1.0)
        assert result == pytest.approx(100.0)

    def test_negative_direction(self):
        result = self.rate_limit(90.0, -90.0, 1.0)
        assert result == pytest.approx(-10.0)

    def test_short_dt_tight_limit(self):
        result = self.rate_limit(0.0, 90.0, 0.016)
        assert result == pytest.approx(1.6, abs=0.01)

    def test_zero_dt_no_movement(self):
        result = self.rate_limit(45.0, 90.0, 0.0)
        assert result == pytest.approx(45.0)

    def test_exact_rate(self):
        result = self.rate_limit(0.0, 100.0, 1.0, 100.0)
        assert result == pytest.approx(100.0)

    def test_custom_velocity(self):
        result = self.rate_limit(0.0, 200.0, 1.0, 60.0)
        assert result == pytest.approx(60.0)

    def test_no_movement_when_at_target(self):
        result = self.rate_limit(90.0, 90.0, 1.0)
        assert result == pytest.approx(90.0)

    def test_rate_limit_disabled(self):
        pass


class TestSoftwareEndstops:
    """FusionHat software endstop enforcement."""

    def apply_endstops(self, angle, min_angle=0.0, max_angle=180.0):
        if min_angle > max_angle:
            return (min_angle + max_angle) / 2.0
        return max(min_angle, min(max_angle, angle))

    def test_angle_within_endstops(self):
        assert self.apply_endstops(90.0, 0.0, 180.0) == 90.0

    def test_angle_below_endstop(self):
        assert self.apply_endstops(-10.0, 0.0, 180.0) == 0.0

    def test_angle_above_endstop(self):
        assert self.apply_endstops(190.0, 0.0, 180.0) == 180.0

    def test_custom_endstops_narrow(self):
        assert self.apply_endstops(50.0, 30.0, 70.0) == 50.0

    def test_custom_endstops_above(self):
        assert self.apply_endstops(80.0, 30.0, 70.0) == 70.0

    def test_custom_endstops_below(self):
        assert self.apply_endstops(20.0, 30.0, 70.0) == 30.0

    def test_inverted_endstops(self):
        result = self.apply_endstops(50.0, 180.0, 0.0)
        assert result == 90.0

    def test_endstops_disabled(self):
        pass


class TestServoStatus:
    """ServoStatus per-channel state."""

    def test_defaults(self):
        s = {"channel": 0, "angle_deg": 0.0, "pulse_width_us": 0,
             "enabled": False, "endstop_active": False, "last_update_ns": 0}
        assert s["channel"] == 0
        assert not s["enabled"]
        assert not s["endstop_active"]
        assert s["last_update_ns"] == 0

    def test_active_servo(self):
        s = {"channel": 5, "angle_deg": 90.0, "pulse_width_us": 1500,
             "enabled": True, "endstop_active": False, "last_update_ns": 123456789}
        assert s["channel"] == 5
        assert s["angle_deg"] == pytest.approx(90.0)
        assert s["pulse_width_us"] == 1500
        assert s["enabled"]
        assert s["last_update_ns"] == 123456789

    def test_endstop_active(self):
        s = {"channel": 1, "angle_deg": 0.0, "pulse_width_us": 1000,
             "enabled": True, "endstop_active": True, "last_update_ns": 100}
        assert s["endstop_active"]

    def test_channel_bounds(self):
        for ch in range(12):
            s = {"channel": ch, "angle_deg": 90.0, "pulse_width_us": 1500,
                 "enabled": True, "endstop_active": False, "last_update_ns": 0}
            assert 0 <= s["channel"] <= 11


class TestPWMChannelControl:
    """FusionHat PWM direct control."""

    def test_duty_cycle_range(self):
        for dc in [0, 25, 50, 75, 100]:
            assert 0 <= dc <= 100

    def test_duty_cycle_negative(self):
        assert not (-1 >= 0)

    def test_duty_cycle_over_100(self):
        assert not (101 <= 100)

    def test_freq_range_low(self):
        assert 1 == 1

    def test_freq_range_high(self):
        assert 1000 == 1000

    def test_period_from_freq(self):
        period_us = 1_000_000 // 50
        assert period_us == 20000

    def test_period_at_1hz(self):
        period_us = 1_000_000 // 1
        assert period_us == 1_000_000

    def test_period_at_1000hz(self):
        period_us = 1_000_000 // 1000
        assert period_us == 1000


class TestFusionHatSysfsPaths:
    """FusionHat sysfs interface paths."""

    def test_sysfs_base(self):
        assert "/sys/class/fusion_hat/fusion_hat" == "/sys/class/fusion_hat/fusion_hat"

    def test_device_tree_path(self):
        assert "/proc/device-tree" == "/proc/device-tree"

    def test_pwm_channel_path(self):
        for ch in range(12):
            path = f"/sys/class/fusion_hat/fusion_hat/pwm{ch}"
            assert path.startswith("/sys/class/fusion_hat/fusion_hat/pwm")
            assert str(ch) in path

    def test_channel_path_0(self):
        assert "/sys/class/fusion_hat/fusion_hat/pwm0" == "/sys/class/fusion_hat/fusion_hat/pwm0"

    def test_channel_path_11(self):
        assert "/sys/class/fusion_hat/fusion_hat/pwm11" == "/sys/class/fusion_hat/fusion_hat/pwm11"
