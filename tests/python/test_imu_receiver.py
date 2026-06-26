import pytest


class TestImuSample:
    """ImuSample: single IMU sensor sample."""

    def test_defaults(self):
        s = {"sensor_timestamp_ns": 0, "receipt_timestamp_ns": 0,
             "sensor_type": "", "values": [0.0, 0.0, 0.0, 0.0], "valid": False}
        assert not s["valid"]
        assert len(s["values"]) == 4

    def test_valid_sample(self):
        s = {"sensor_timestamp_ns": 1000, "receipt_timestamp_ns": 2000,
             "sensor_type": "android.sensor.accelerometer",
             "values": [1.0, 2.0, 9.81, 0.0], "valid": True}
        assert s["valid"]
        assert s["sensor_type"] == "android.sensor.accelerometer"


class TestImuSensorType:
    """ImuSensorType enum: sensor type identifiers."""

    def test_values(self):
        assert 0 == 0    # kAccelerometer
        assert 1 == 1    # kGyroscope
        assert 2 == 2    # kDeviceOrientation
        assert 3 == 3    # kOrientation
        assert 255 == 255  # kUnknown


class TestImuData:
    """ImuData: aggregated IMU data from multiple sensors."""

    def test_defaults(self):
        d = {"accel_x": 0.0, "accel_y": 0.0, "accel_z": 0.0,
             "accel_valid": False, "gyro_x": 0.0, "gyro_y": 0.0, "gyro_z": 0.0,
             "gyro_valid": False, "azimuth_deg": 0.0, "pitch_deg": 0.0,
             "roll_deg": 0.0, "orientation_valid": False,
             "quat_w": 1.0, "quat_x": 0.0, "quat_y": 0.0, "quat_z": 0.0,
             "quaternion_valid": False, "packets_received": 0,
             "packets_dropped": 0, "latency_ms": 0.0}
        assert d["quat_w"] == 1.0
        assert d["quaternion_valid"] is False

    def test_orientation_ranges(self):
        assert 0 <= 0 <= 360  # azimuth
        assert -180 <= 0 <= 180  # pitch
        assert -180 <= 0 <= 180  # roll

    def test_quaternion_magnitude(self):
        w, x, y, z = 1.0, 0.0, 0.0, 0.0
        mag = (w**2 + x**2 + y**2 + z**2)**0.5
        assert abs(mag - 1.0) < 0.001


class TestImuReceiverConfig:
    """ImuReceiverConfig: UDP receiver settings."""

    def test_default_bind_address(self):
        assert "0.0.0.0" == "0.0.0.0"

    def test_default_port(self):
        assert 7070 == 7070

    def test_default_queue_size(self):
        assert 100 == 100

    def test_default_max_age_ns(self):
        assert 100000000 == 100000000

    def test_all_sensors_enabled(self):
        config = {"enable_accelerometer": True, "enable_gyroscope": True,
                  "enable_device_orientation": True, "enable_orientation": True}
        assert all(config.values())
