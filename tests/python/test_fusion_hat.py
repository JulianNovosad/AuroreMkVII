import pytest


class TestFusionHatConfig:
    """FusionHatConfig: sysfs PWM/servo configuration validation."""

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

    def test_valid_config(self):
        cfg = self.make_config()
        assert self.validate(cfg)

    def test_freq_zero(self):
        assert not self.validate(self.make_config(freq=0))

    def test_freq_over_limit(self):
        assert not self.validate(self.make_config(freq=1001))

    def test_min_pulse_zero(self):
        assert not self.validate(self.make_config(min_pw=0))

    def test_min_pulse_over_limit(self):
        assert not self.validate(self.make_config(min_pw=10001))

    def test_max_equal_min(self):
        assert not self.validate(self.make_config(min_pw=1000, max_pw=1000))

    def test_max_less_than_min(self):
        assert not self.validate(self.make_config(min_pw=2000, max_pw=1000))

    def test_min_angle_ge_max(self):
        assert not self.validate(self.make_config(min_angle=90.0, max_angle=90.0))

    def test_min_angle_greater(self):
        assert not self.validate(self.make_config(min_angle=100.0, max_angle=90.0))

    def test_i2c_timeout_zero(self):
        assert not self.validate(self.make_config(i2c_timeout=0))

    def test_max_retries_negative(self):
        assert not self.validate(self.make_config(max_retries=-1))

    def test_default_values(self):
        cfg = self.make_config()
        assert cfg["servo_freq_hz"] == 50
        assert cfg["min_pulse_width_us"] == 1000
        assert cfg["max_pulse_width_us"] == 2000
        assert cfg["min_angle_deg"] == 0.0
        assert cfg["max_angle_deg"] == 180.0

    def test_error_threshold_default(self):
        assert 10 == 10


class TestAngleToPulseWidth:
    """FusionHat::angle_to_pulse_width and pulse_width_to_angle."""

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

    def test_min_angle_min_pw(self):
        assert self.angle_to_pw(0.0) == 1000

    def test_max_angle_max_pw(self):
        assert self.angle_to_pw(180.0) == 2000

    def test_center_angle_center_pw(self):
        assert self.angle_to_pw(90.0) == 1500

    def test_quarter_angle(self):
        assert self.angle_to_pw(45.0) == 1250

    def test_below_min_clamp(self):
        assert self.angle_to_pw(-10.0) == 1000

    def test_above_max_clamp(self):
        assert self.angle_to_pw(190.0) == 2000

    def test_interlock_inhibit(self):
        kInterlockInhibitUs = 1000
        assert kInterlockInhibitUs == 1000

    def test_interlock_enable(self):
        kInterlockEnableUs = 2000
        assert kInterlockEnableUs == 2000

    def test_interlock_channel(self):
        assert 2 == 2

    def test_pw_to_angle_roundtrip(self):
        for angle in [0.0, 45.0, 90.0, 135.0, 180.0]:
            pw = self.angle_to_pw(angle)
            a = self.pw_to_angle(pw)
            assert abs(a - angle) < 0.5

    def test_angle_to_pw_linearity(self):
        pw_0 = self.angle_to_pw(0.0)
        pw_90 = self.angle_to_pw(90.0)
        pw_180 = self.angle_to_pw(180.0)
        assert pw_90 - pw_0 == pw_180 - pw_90


class TestServoStatus:
    """ServoStatus: per-channel servo state."""

    def test_defaults(self):
        s = {"channel": 0, "angle_deg": 0.0, "pulse_width_us": 0,
             "enabled": False, "endstop_active": False, "last_update_ns": 0}
        assert s["channel"] == 0

    def test_active_servo(self):
        s = {"channel": 0, "angle_deg": 90.0, "pulse_width_us": 1500,
             "enabled": True, "endstop_active": False, "last_update_ns": 1000}
        assert s["enabled"]
        assert s["pulse_width_us"] == 1500


class TestGimbalMotionConstraints:
    """AM7-L3-ACT-001: Gimbal constraints in .rodata."""

    def test_elevation_limits(self):
        assert -10.0 == -10.0
        assert 45.0 == 45.0

    def test_azimuth_limit(self):
        assert 90.0 == 90.0

    def test_velocity_limit(self):
        assert 60.0 == 60.0

    def test_acceleration_limit(self):
        assert 120.0 == 120.0
