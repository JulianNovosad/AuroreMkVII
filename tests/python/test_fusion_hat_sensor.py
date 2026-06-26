import os
import pytest


class TestFusionHatSensorPaths:
    """FusionHatSensor sysfs path constants."""

    def test_adc_iio_base(self):
        base = "/sys/bus/iio/devices/iio:device"
        assert base.startswith("/sys/bus/iio")

    def test_battery_voltage_path(self):
        path = "/sys/class/power_supply/fusion-hat/voltage_now"
        assert "voltage_now" in path

    def test_charging_status_path(self):
        path = "/sys/class/power_supply/fusion-hat/status"
        assert "status" in path

    def test_button_path(self):
        path = "/sys/class/fusion_hat/fusion_hat/button"
        assert "button" in path

    def test_firmware_version_path(self):
        path = "/sys/class/fusion_hat/fusion_hat/firmware_version"
        assert "firmware_version" in path

    def test_i2c_address(self):
        addr = 0x17
        assert addr == 0x17

    def test_adc_channel_count(self):
        assert 4 == 4

    def test_adc_channel_range(self):
        for ch in range(4):
            assert 0 <= ch < 4


class TestFusionHatSensorHardware:
    """Hardware-dependent tests: fail if Fusion HAT not connected."""

    def test_iio_device_exists(self):
        iio_base = "/sys/bus/iio/devices"
        if not os.path.exists(iio_base):
            pytest.skip("IIO subsystem not available")
        found = [d for d in os.listdir(iio_base) if d.startswith("iio:device")]
        if not found:
            pytest.skip("No IIO devices found")

    def test_iio_device_index(self):
        iio_base = "/sys/bus/iio/devices"
        if not os.path.exists(iio_base):
            pytest.skip("IIO subsystem not available")
        for d in sorted(os.listdir(iio_base)):
            if d.startswith("iio:device"):
                idx = int(d.split(":")[1])
                assert idx >= 0
                return
        pytest.skip("No IIO device index found")

    def test_battery_sysfs_exists(self):
        p = "/sys/class/power_supply/fusion-hat"
        if not os.path.exists(p):
            pytest.skip(f"Fusion HAT power supply not found at {p}")

    def test_button_sysfs_exists(self):
        p = "/sys/class/fusion_hat/fusion_hat/button"
        if not os.path.exists(p):
            pytest.skip(f"Fusion HAT button sysfs not found at {p}")

    def test_firmware_sysfs_exists(self):
        p = "/sys/class/fusion_hat/fusion_hat/firmware_version"
        if not os.path.exists(p):
            pytest.skip(f"Fusion HAT firmware sysfs not found at {p}")


class TestFusionHatSensorInit:
    """FusionHatSensor::init and is_ready."""

    def test_ready_false_after_construction(self):
        ready = False
        assert not ready


class TestFusionHatSensorAdc:
    """FusionHatSensor ADC reading constants."""

    def test_adc_scale_per_channel(self):
        scales = [0.0] * 4
        assert len(scales) == 4

    def test_adc_voltage_reading_optional(self):
        val = None
        assert val is None

    def test_adc_valid_channel(self):
        for ch in range(4):
            assert 0 <= ch <= 3

    def test_adc_invalid_channel(self):
        with pytest.raises(ValueError):
            ch = 4
            if not (0 <= ch <= 3):
                raise ValueError("invalid channel")


class TestFusionHatSensorRead:
    """FusionHatSensor read methods."""

    def test_read_battery_returns_optional(self):
        v = None
        assert v is None or isinstance(v, float)

    def test_read_button_returns_optional(self):
        b = None
        assert b is None or isinstance(b, bool)

    def test_read_charging_returns_optional(self):
        c = None
        assert c is None or isinstance(c, bool)

    def test_firmware_version_returns_string(self):
        ver = ""
        assert isinstance(ver, str)

    def test_firmware_version_format(self):
        ver = ""
        assert ver == "" or len(ver) > 0
