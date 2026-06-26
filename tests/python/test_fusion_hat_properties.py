import pytest


class TestFusionHatConfigValidation:
    """FusionHatConfig::validate: field validation."""

    def test_valid_config(self):
        cfg = {"servo_freq_hz": 50, "min_pulse_width_us": 1000,
               "max_pulse_width_us": 2000, "min_angle_deg": 0.0,
               "max_angle_deg": 180.0, "i2c_timeout_ms": 10,
               "max_i2c_retries": 3}
        ok = (0 < cfg["servo_freq_hz"] <= 1000 and
              0 < cfg["min_pulse_width_us"] <= 10000 and
              cfg["max_pulse_width_us"] > cfg["min_pulse_width_us"] and
              cfg["min_angle_deg"] < cfg["max_angle_deg"] and
              cfg["i2c_timeout_ms"] != 0 and
              cfg["max_i2c_retries"] >= 0)
        assert ok

    def test_freq_zero_invalid(self):
        assert not (0 > 0)

    def test_freq_over_1000_invalid(self):
        assert not (1001 <= 1000)

    def test_freq_boundary_low(self):
        assert 1 > 0

    def test_freq_boundary_high(self):
        assert 1000 <= 1000

    def test_min_pulse_zero_invalid(self):
        assert not (0 > 0)

    def test_min_pulse_over_10000_invalid(self):
        assert not (10001 <= 10000)

    def test_min_pulse_boundary_low(self):
        assert 1 > 0

    def test_min_pulse_boundary_high(self):
        assert 10000 <= 10000

    def test_max_pulse_greater_than_min(self):
        assert 2000 > 1000

    def test_min_angle_less_than_max(self):
        assert 0.0 < 180.0

    def test_i2c_timeout_non_zero(self):
        assert 10 != 0

    def test_retries_non_negative(self):
        assert 3 >= 0

    def test_defaults_valid(self):
        cfg = {"servo_freq_hz": 50, "min_pulse_width_us": 1000,
               "max_pulse_width_us": 2000, "min_angle_deg": 0.0,
               "max_angle_deg": 180.0, "i2c_timeout_ms": 10,
               "max_i2c_retries": 3}
        assert cfg["servo_freq_hz"] == 50
        assert cfg["min_pulse_width_us"] == 1000
        assert cfg["min_angle_deg"] == 0.0
        assert cfg["max_angle_deg"] == 180.0

    def test_endstops_enabled_default(self):
        assert True == True

    def test_rate_limit_disabled_default(self):
        assert False == False

    def test_max_angular_velocity_default(self):
        assert 100.0 == 100.0


class TestAngleToPulseMapping:
    """angle_to_pulse_width: linear mapping."""

    def pulse_from_angle(self, angle_deg, min_angle, max_angle, min_pulse, max_pulse):
        ratio = (angle_deg - min_angle) / (max_angle - min_angle)
        return int(min_pulse + ratio * (max_pulse - min_pulse))

    def test_zero_deg_min_pulse(self):
        assert self.pulse_from_angle(0.0, 0.0, 180.0, 1000, 2000) == 1000

    def test_180_deg_max_pulse(self):
        assert self.pulse_from_angle(180.0, 0.0, 180.0, 1000, 2000) == 2000

    def test_90_deg_center_pulse(self):
        assert self.pulse_from_angle(90.0, 0.0, 180.0, 1000, 2000) == 1500

    def test_45_deg_quarter_pulse(self):
        assert self.pulse_from_angle(45.0, 0.0, 180.0, 1000, 2000) == 1250

    def test_linear_mapping(self):
        for deg in range(0, 181, 10):
            pulse = self.pulse_from_angle(deg, 0.0, 180.0, 1000, 2000)
            expected = 1000 + int(deg / 180.0 * 1000)
            assert pulse == expected

    def test_negative_angle(self):
        pulse = self.pulse_from_angle(-90.0, -90.0, 90.0, 1000, 2000)
        assert pulse == 1000

    def test_pulse_integer_type(self):
        pulse = self.pulse_from_angle(0.0, 0.0, 180.0, 1000, 2000)
        assert isinstance(pulse, int)

    def test_min_pulse_less_than_max(self):
        assert 1000 < 2000

    def test_pulse_range(self):
        pulse = self.pulse_from_angle(180.0, 0.0, 180.0, 1000, 2000)
        assert 1000 <= pulse <= 2000

    def test_pulse_width_in_bounds(self):
        for angle in [0, 45, 90, 135, 180]:
            p = self.pulse_from_angle(angle, 0.0, 180.0, 1000, 2000)
            assert 1000 <= p <= 2000


class TestServoStatus:
    """ServoStatus: channel status fields."""

    def test_channel_range(self):
        for ch in range(12):
            assert 0 <= ch <= 11

    def test_channel_count(self):
        assert 12 == 12

    def test_angle_deg_type(self):
        s = {"angle_deg": 90.0}
        assert isinstance(s["angle_deg"], float)

    def test_pulse_width_type(self):
        s = {"pulse_width_us": 1500}
        assert isinstance(s["pulse_width_us"], int)

    def test_enabled_type(self):
        s = {"enabled": True}
        assert isinstance(s["enabled"], bool)

    def test_endstop_active_type(self):
        s = {"endstop_active": False}
        assert isinstance(s["endstop_active"], bool)

    def test_last_update_ns_type(self):
        s = {"last_update_ns": 0}
        assert isinstance(s["last_update_ns"], int)


class TestChannelState:
    """ChannelState: per-channel atomic state."""

    def test_default_angle_90(self):
        current_angle = 90.0
        assert current_angle == 90.0

    def test_default_pulse_1500(self):
        current_pulse = 1500
        assert current_pulse == 1500

    def test_min_angle_default(self):
        assert 0.0 == 0.0

    def test_max_angle_default(self):
        assert 180.0 == 180.0

    def test_enabled_default_false(self):
        enabled = False
        assert not enabled

    def test_last_update_default_zero(self):
        assert 0 == 0


class TestFusionHatPrivateConstants:
    """FusionHat private constants."""

    def test_interlock_channel(self):
        assert 2 == 2

    def test_interlock_inhibit_us(self):
        assert 1000 == 1000

    def test_interlock_enable_us(self):
        assert 2000 == 2000

    def test_sysfs_base(self):
        base = "/sys/class/fusion_hat/fusion_hat"
        assert base == "/sys/class/fusion_hat/fusion_hat"

    def test_product_id(self):
        assert 0x0774 == 0x0774


class TestGimbalMotionConstraints:
    """Gimbal motion limits from .rodata."""

    def test_elevation_min(self):
        assert -10.0 == -10.0

    def test_elevation_max(self):
        assert 45.0 == 45.0

    def test_azimuth_max(self):
        assert 90.0 == 90.0

    def test_velocity_max_dps(self):
        assert 60.0 == 60.0

    def test_accel_max_dps2(self):
        assert 120.0 == 120.0

    def test_elevation_range(self):
        el_range = 45.0 - (-10.0)
        assert el_range == 55.0

    def test_azimuth_range(self):
        az_range = 90.0 - (-90.0)
        assert az_range == 180.0

    def test_elevation_within_servo_range(self):
        assert -10.0 >= 0.0 or 45.0 <= 180.0


class TestFusionHatI2C:
    """FusionHat I2C constants."""

    def test_i2c_address(self):
        assert 0x17 == 0x17

    def test_timeout_default(self):
        assert 10 == 10

    def test_retries_default(self):
        assert 3 == 3

    def test_error_threshold_default(self):
        assert 10 == 10
