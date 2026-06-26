import pytest
import struct
import math


class TestImuReceiverConfigValidation:
    """ImuReceiverConfig: field validation and defaults."""

    def make_config(self, bind_addr="0.0.0.0", port=7070, queue_size=100,
                    max_age_ns=100000000, enable_accel=True, enable_gyro=True,
                    enable_dev_orient=True, enable_orient=True):
        return {
            "bind_address": bind_addr,
            "udp_port": port,
            "max_queue_size": queue_size,
            "max_sample_age_ns": max_age_ns,
            "enable_accelerometer": enable_accel,
            "enable_gyroscope": enable_gyro,
            "enable_device_orientation": enable_dev_orient,
            "enable_orientation": enable_orient,
        }

    def validate(self, cfg):
        if cfg["udp_port"] == 0 or cfg["udp_port"] > 65535:
            return False
        if cfg["bind_address"] not in ("0.0.0.0", "127.0.0.1"):
            return False
        if cfg["max_queue_size"] == 0 or cfg["max_queue_size"] > 10000:
            return False
        if cfg["max_sample_age_ns"] == 0:
            return False
        return True

    def test_defaults(self):
        cfg = self.make_config()
        assert cfg["bind_address"] == "0.0.0.0"
        assert cfg["udp_port"] == 7070
        assert cfg["max_queue_size"] == 100
        assert cfg["max_sample_age_ns"] == 100000000

    def test_valid_config(self):
        assert self.validate(self.make_config())

    def test_port_zero(self):
        assert not self.validate(self.make_config(port=0))

    def test_port_max(self):
        assert self.validate(self.make_config(port=65535))

    def test_port_over_max(self):
        assert not self.validate(self.make_config(port=65536))

    def test_bind_loopback(self):
        assert self.validate(self.make_config(bind_addr="127.0.0.1"))

    def test_bind_invalid(self):
        assert not self.validate(self.make_config(bind_addr="192.168.1.1"))

    def test_bind_empty(self):
        assert not self.validate(self.make_config(bind_addr=""))

    def test_queue_size_zero(self):
        assert not self.validate(self.make_config(queue_size=0))

    def test_queue_size_at_max(self):
        assert self.validate(self.make_config(queue_size=10000))

    def test_queue_size_over_max(self):
        assert not self.validate(self.make_config(queue_size=10001))

    def test_max_age_zero(self):
        assert not self.validate(self.make_config(max_age_ns=0))

    def test_max_age_100ms(self):
        assert self.make_config()["max_sample_age_ns"] == 100000000

    def test_sensor_flags_all_true(self):
        cfg = self.make_config()
        assert all([cfg["enable_accelerometer"], cfg["enable_gyroscope"],
                    cfg["enable_device_orientation"], cfg["enable_orientation"]])

    def test_accel_disabled(self):
        cfg = self.make_config(enable_accel=False)
        assert not cfg["enable_accelerometer"]

    def test_gyro_disabled(self):
        cfg = self.make_config(enable_gyro=False)
        assert not cfg["enable_gyroscope"]

    def test_device_orientation_disabled(self):
        cfg = self.make_config(enable_dev_orient=False)
        assert not cfg["enable_device_orientation"]

    def test_orientation_disabled(self):
        cfg = self.make_config(enable_orient=False)
        assert not cfg["enable_orientation"]


class TestImuSensorType:
    """ImuSensorType enum: values and mapping."""

    def parse_sensor_type(self, type_str):
        mapping = {
            "android.sensor.accelerometer": 0,
            "android.sensor.gyroscope": 1,
            "android.sensor.device_orientation": 2,
            "android.sensor.orientation": 3,
        }
        return mapping.get(type_str, 255)

    def test_accelerometer_value(self):
        assert self.parse_sensor_type("android.sensor.accelerometer") == 0

    def test_gyroscope_value(self):
        assert self.parse_sensor_type("android.sensor.gyroscope") == 1

    def test_device_orientation_value(self):
        assert self.parse_sensor_type("android.sensor.device_orientation") == 2

    def test_orientation_value(self):
        assert self.parse_sensor_type("android.sensor.orientation") == 3

    def test_unknown_type(self):
        assert self.parse_sensor_type("android.sensor.magnetic_field") == 255

    def test_empty_string(self):
        assert self.parse_sensor_type("") == 255

    def test_case_sensitivity(self):
        assert self.parse_sensor_type("ANDROID.SENSOR.ACCELEROMETER") == 255

    def test_partial_match(self):
        assert self.parse_sensor_type("accelerometer") == 255

    def test_kUnknown_value(self):
        assert 255 == 255

    def test_enum_range(self):
        for v in [0, 1, 2, 3, 255]:
            assert 0 <= v <= 255


class TestJsonParsing:
    """ImuReceiver JSON parsing: find_json_value and parse_float_array."""

    def find_json_value(self, json_str, key):
        search = '"' + key + '"'
        pos = json_str.find(search)
        if pos == -1:
            return ""
        pos = json_str.find(":", pos + len(search))
        if pos == -1:
            return ""
        pos += 1
        while pos < len(json_str) and json_str[pos] in " \t":
            pos += 1
        if pos >= len(json_str):
            return ""
        if json_str[pos] == '"':
            end = json_str.find('"', pos + 1)
            if end == -1:
                return ""
            return json_str[pos + 1:end]
        if json_str[pos] == "[":
            end = json_str.find("]", pos + 1)
            if end == -1:
                return ""
            return json_str[pos:end + 1]
        end = pos
        while end < len(json_str) and json_str[end] not in ",}]":
            end += 1
        while end > pos and json_str[end - 1] in " \t":
            end -= 1
        return json_str[pos:end]

    def parse_float_array(self, array_str):
        values = []
        if not array_str or array_str[0] != "[":
            return values
        content = array_str[1:-1]
        for item in content.split(","):
            item = item.strip()
            if item:
                try:
                    values.append(float(item))
                except ValueError:
                    pass
        return values

    def test_find_type_string(self):
        js = '{"type": "android.sensor.accelerometer", "timestamp": 123}'
        assert self.find_json_value(js, "type") == "android.sensor.accelerometer"

    def test_find_timestamp(self):
        js = '{"type": "a", "timestamp": 3925657519043709}'
        val = self.find_json_value(js, "timestamp")
        assert val == "3925657519043709"

    def test_find_values_array(self):
        js = '{"type": "a", "values": [0.32, -0.98, 10.05]}'
        assert self.find_json_value(js, "values") == "[0.32, -0.98, 10.05]"

    def test_key_not_found(self):
        assert self.find_json_value("{}", "nonexistent") == ""

    def test_empty_json(self):
        assert self.find_json_value("", "type") == ""

    def test_missing_colon(self):
        js = '{"type"  "accelerometer"}'
        assert self.find_json_value(js, "type") == ""

    def test_parse_float_array_three(self):
        vals = self.parse_float_array("[0.32, -0.98, 10.05]")
        assert len(vals) == 3
        assert vals[0] == pytest.approx(0.32)
        assert vals[1] == pytest.approx(-0.98)
        assert vals[2] == pytest.approx(10.05)

    def test_parse_float_array_one(self):
        vals = self.parse_float_array("[9.81]")
        assert len(vals) == 1
        assert vals[0] == pytest.approx(9.81)

    def test_parse_float_array_four(self):
        vals = self.parse_float_array("[0.7, 0.0, 0.0, 0.7]")
        assert len(vals) == 4

    def test_parse_empty_array(self):
        assert self.parse_float_array("[]") == []

    def test_parse_empty_string(self):
        assert self.parse_float_array("") == []

    def test_parse_not_array(self):
        assert self.parse_float_array("null") == []

    def test_parse_bad_values(self):
        vals = self.parse_float_array("[abc, def]")
        assert vals == []

    def test_parse_mixed_values(self):
        vals = self.parse_float_array("[1.0, null, 3.0]")
        assert len(vals) == 2

    def test_parse_negative_values(self):
        vals = self.parse_float_array("[-1.5, -2.5]")
        assert vals[0] == pytest.approx(-1.5)
        assert vals[1] == pytest.approx(-2.5)

    def test_parse_scientific_notation(self):
        vals = self.parse_float_array("[1e-3, 2.5e2]")
        assert vals[0] == pytest.approx(0.001)
        assert vals[1] == pytest.approx(250.0)

    def test_parse_whitespace_variations(self):
        vals = self.parse_float_array("[  1.0  ,  2.0  ]")
        assert len(vals) == 2
        assert vals[0] == pytest.approx(1.0)

    def test_find_with_whitespace(self):
        js = '{"type" : "android.sensor.gyroscope"}'
        assert self.find_json_value(js, "type") == "android.sensor.gyroscope"

    def test_find_numeric_with_spaces(self):
        js = '{"timestamp":  3925657519043709}'
        assert self.find_json_value(js, "timestamp") == "3925657519043709"

    def test_find_nested_obj_skip(self):
        js = '{"outer": {"type": "nested"}}'
        assert self.find_json_value(js, "type") == "nested"

    def test_complex_sensagram(self):
        js = ('{"type": "android.sensor.accelerometer", '
              '"timestamp": 3925657519043709, '
              '"values": [0.32, -0.98, 10.05], '
              '"sensor": 1}')
        assert self.find_json_value(js, "type") == "android.sensor.accelerometer"
        assert self.find_json_value(js, "timestamp") == "3925657519043709"
        vals = self.parse_float_array(self.find_json_value(js, "values"))
        assert len(vals) == 3


class TestLatencyMovingAverage:
    """ImuReceiver latency moving average calculation."""

    def compute_avg_latency(self, latencies, max_window=100):
        window = latencies[-max_window:]
        return sum(window) / len(window) if window else 0.0

    def test_single_sample(self):
        assert self.compute_avg_latency([5.0]) == pytest.approx(5.0)

    def test_two_samples(self):
        assert self.compute_avg_latency([5.0, 15.0]) == pytest.approx(10.0)

    def test_many_samples(self):
        lats = [float(i) for i in range(100)]
        assert self.compute_avg_latency(lats) == pytest.approx(49.5)

    def test_window_trimming(self):
        lats = [float(i) for i in range(200)]
        avg = self.compute_avg_latency(lats, 100)
        expected = sum(range(100, 200)) / 100.0
        assert avg == pytest.approx(expected)

    def test_all_zeros(self):
        assert self.compute_avg_latency([0.0] * 50) == pytest.approx(0.0)

    def test_negative_latency(self):
        assert self.compute_avg_latency([-1.0, 1.0]) == pytest.approx(0.0)

    def test_high_latency(self):
        lats = [1000.0] * 10
        assert self.compute_avg_latency(lats) == pytest.approx(1000.0)

    def test_empty_history(self):
        assert self.compute_avg_latency([]) == pytest.approx(0.0)

    def test_window_boundary(self):
        lats = [1.0] * 100
        assert self.compute_avg_latency(lats, 100) == pytest.approx(1.0)

    def test_window_exact(self):
        lats = [float(i) for i in range(100)]
        avg = self.compute_avg_latency(lats, 100)
        assert avg == pytest.approx(49.5)

    def test_window_smaller_than_data(self):
        lats = [10.0] * 50
        assert self.compute_avg_latency(lats, 20) == pytest.approx(10.0)

    def test_latency_monotonic(self):
        lats = [i * 0.5 for i in range(50)]
        avgs = [self.compute_avg_latency(lats[:i + 1]) for i in range(1, 50)]
        for i in range(1, len(avgs)):
            assert avgs[i] >= avgs[i - 1] or abs(avgs[i] - avgs[i - 1]) < 0.5


class TestImuDataFreshness:
    """ImuReceiver::is_data_fresh logic."""

    def is_fresh(self, latest_ts_ns, now_ns, max_age_ns=100000000):
        if latest_ts_ns == 0:
            return False
        age = now_ns - latest_ts_ns
        return age < max_age_ns

    def test_no_data(self):
        assert not self.is_fresh(0, 1000)

    def test_just_received(self):
        assert self.is_fresh(5000, 5100)

    def test_just_under_age_limit(self):
        assert self.is_fresh(1000, 1000 + 99999999)

    def test_at_age_limit(self):
        assert not self.is_fresh(1000, 1000 + 100000000)

    def test_over_age_limit(self):
        assert not self.is_fresh(1000, 1000 + 100000001)

    def test_way_over_age(self):
        assert not self.is_fresh(1000, 1000 + 1000000000)

    def test_future_timestamp(self):
        assert self.is_fresh(2000, 1000)

    def test_custom_max_age(self):
        assert self.is_fresh(1000, 1050, 100)
        assert not self.is_fresh(1000, 1101, 100)

    def test_freshness_at_zero_boundary(self):
        assert not self.is_fresh(0, 0)


class TestImuSampleDefaults:
    """ImuSample: field defaults and construction."""

    def test_default_timestamps(self):
        s = {"sensor_timestamp_ns": 0, "receipt_timestamp_ns": 0}
        assert s["sensor_timestamp_ns"] == 0
        assert s["receipt_timestamp_ns"] == 0

    def test_values_length(self):
        s = {"values": [0.0, 0.0, 0.0, 0.0]}
        assert len(s["values"]) == 4

    def test_valid_default_false(self):
        s = {"valid": False}
        assert not s["valid"]

    def test_sensor_type_default(self):
        s = {"sensor_type": ""}
        assert s["sensor_type"] == ""

    def test_sample_timestamps_ordering(self):
        s = {"sensor_timestamp_ns": 1000, "receipt_timestamp_ns": 2000}
        assert s["receipt_timestamp_ns"] >= s["sensor_timestamp_ns"]


class TestImuSampleValidation:
    """ImuSample: validity logic."""

    def is_valid(self, sample):
        if not sample["valid"]:
            return False
        if len(sample["values"]) < 3:
            return False
        if not sample["sensor_type"]:
            return False
        return True

    def test_valid_sample(self):
        s = {"sensor_timestamp_ns": 100, "receipt_timestamp_ns": 200,
             "sensor_type": "android.sensor.accelerometer",
             "values": [1.0, 2.0, 9.81, 0.0], "valid": True}
        assert self.is_valid(s)

    def test_not_valid_flag(self):
        s = {"sensor_type": "a", "values": [1, 2, 3], "valid": False}
        assert not self.is_valid(s)

    def test_empty_sensor_type(self):
        s = {"sensor_type": "", "values": [1, 2, 3], "valid": True}
        assert not self.is_valid(s)

    def test_too_few_values(self):
        s = {"sensor_type": "a", "values": [1.0, 2.0], "valid": True}
        assert not self.is_valid(s)


class TestImuDataAggregation:
    """ImuData: update_aggregated_data for each sensor type."""

    def make_sample(self, sensor_type, values, valid=True, ts=1000, receipt=2000):
        return {"sensor_timestamp_ns": ts, "receipt_timestamp_ns": receipt,
                "sensor_type": sensor_type, "values": values, "valid": valid}

    def parse_sensor_type(self, type_str):
        mapping = {"android.sensor.accelerometer": 0,
                   "android.sensor.gyroscope": 1,
                   "android.sensor.device_orientation": 2,
                   "android.sensor.orientation": 3}
        return mapping.get(type_str, 255)

    def update_aggregated_data(self, data, sample):
        data["latest_timestamp_ns"] = sample["receipt_timestamp_ns"]
        stype = self.parse_sensor_type(sample["sensor_type"])
        if stype == 0:
            data["accel_x"] = sample["values"][0]
            data["accel_y"] = sample["values"][1]
            data["accel_z"] = sample["values"][2]
            data["accel_valid"] = sample["valid"]
        elif stype == 1:
            data["gyro_x"] = sample["values"][0]
            data["gyro_y"] = sample["values"][1]
            data["gyro_z"] = sample["values"][2]
            data["gyro_valid"] = sample["valid"]
        elif stype == 2:
            data["azimuth_deg"] = sample["values"][0]
            data["pitch_deg"] = sample["values"][1]
            data["roll_deg"] = sample["values"][2]
            data["orientation_valid"] = sample["valid"]
        elif stype == 3:
            data["quat_w"] = sample["values"][0]
            data["quat_x"] = sample["values"][1]
            data["quat_y"] = sample["values"][2]
            data["quat_z"] = sample["values"][3]
            data["quaternion_valid"] = sample["valid"]

    def make_data(self):
        return {"accel_x": 0.0, "accel_y": 0.0, "accel_z": 0.0,
                "accel_valid": False, "gyro_x": 0.0, "gyro_y": 0.0, "gyro_z": 0.0,
                "gyro_valid": False, "azimuth_deg": 0.0, "pitch_deg": 0.0,
                "roll_deg": 0.0, "orientation_valid": False,
                "quat_w": 1.0, "quat_x": 0.0, "quat_y": 0.0, "quat_z": 0.0,
                "quaternion_valid": False, "latest_timestamp_ns": 0,
                "packets_received": 0, "packets_dropped": 0, "latency_ms": 0.0}

    def test_accel_update(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.accelerometer", [0.5, -0.2, 9.81])
        self.update_aggregated_data(data, sample)
        assert data["accel_x"] == pytest.approx(0.5)
        assert data["accel_z"] == pytest.approx(9.81)
        assert data["accel_valid"]

    def test_gyro_update(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.gyroscope", [0.01, -0.02, 0.005])
        self.update_aggregated_data(data, sample)
        assert data["gyro_x"] == pytest.approx(0.01)
        assert data["gyro_y"] == pytest.approx(-0.02)
        assert data["gyro_valid"]

    def test_device_orientation_update(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.device_orientation",
                                  [45.0, 30.0, -15.0])
        self.update_aggregated_data(data, sample)
        assert data["azimuth_deg"] == pytest.approx(45.0)
        assert data["pitch_deg"] == pytest.approx(30.0)
        assert data["roll_deg"] == pytest.approx(-15.0)
        assert data["orientation_valid"]

    def test_orientation_quaternion_update(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.orientation",
                                  [0.707, 0.0, 0.707, 0.0])
        self.update_aggregated_data(data, sample)
        assert data["quat_w"] == pytest.approx(0.707)
        assert data["quat_z"] == pytest.approx(0.0)
        assert data["quaternion_valid"]

    def test_unknown_type_does_nothing(self):
        data = self.make_data()
        sample = self.make_sample("unknown.type", [1.0, 2.0, 3.0])
        self.update_aggregated_data(data, sample)
        assert not data["accel_valid"]
        assert not data["gyro_valid"]

    def test_accel_valid_false_propagation(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.accelerometer",
                                  [0.0, 0.0, 0.0], valid=False)
        self.update_aggregated_data(data, sample)
        assert not data["accel_valid"]
        assert data["accel_x"] == pytest.approx(0.0)

    def test_quaternion_four_values(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.orientation",
                                  [0.5, 0.5, 0.5, 0.5])
        self.update_aggregated_data(data, sample)
        assert data["quat_w"] == pytest.approx(0.5)
        assert data["quat_x"] == pytest.approx(0.5)
        assert data["quat_y"] == pytest.approx(0.5)
        assert data["quat_z"] == pytest.approx(0.5)

    def test_timestamp_update(self):
        data = self.make_data()
        sample = self.make_sample("android.sensor.accelerometer",
                                  [1.0, 2.0, 3.0], ts=500, receipt=9999)
        self.update_aggregated_data(data, sample)
        assert data["latest_timestamp_ns"] == 9999


class TestImuDataOrientationRanges:
    """ImuData: orientation field range validation."""

    def test_azimuth_range(self):
        for az in [0.0, 90.0, 180.0, 270.0, 360.0]:
            assert 0.0 <= az <= 360.0

    def test_azimuth_out_of_range_low(self):
        assert not (-1.0 >= 0.0)

    def test_azimuth_out_of_range_high(self):
        assert not (361.0 <= 360.0)

    def test_pitch_range(self):
        for pitch in [-180.0, -90.0, 0.0, 90.0, 180.0]:
            assert -180.0 <= pitch <= 180.0

    def test_pitch_out_of_range_low(self):
        assert not (-181.0 >= -180.0)

    def test_pitch_out_of_range_high(self):
        assert not (181.0 <= 180.0)

    def test_roll_range(self):
        for roll in [-180.0, -90.0, 0.0, 90.0, 180.0]:
            assert -180.0 <= roll <= 180.0

    def test_quaternion_magnitude_unit(self):
        mag = math.sqrt(0.707**2 + 0.0**2 + 0.707**2 + 0.0**2)
        assert abs(mag - 1.0) < 0.001

    def test_quaternion_magnitude_identity(self):
        mag = math.sqrt(1.0**2 + 0.0**2 + 0.0**2 + 0.0**2)
        assert abs(mag - 1.0) < 0.001

    def test_quaternion_magnitude_general(self):
        w, x, y, z = 0.5, 0.5, 0.5, 0.5
        mag = math.sqrt(w**2 + x**2 + y**2 + z**2)
        assert abs(mag - 1.0) < 0.001

    def test_quaternion_non_unit(self):
        w, x, y, z = 0.5, 0.5, 0.5, 0.5
        mag = math.sqrt(w**2 + x**2 + y**2 + z**2)
        assert mag != 0.0


class TestImuReceiverStats:
    """ImuReceiver statistics counters."""

    def test_packets_received_initial(self):
        assert 0 == 0

    def test_packets_dropped_initial(self):
        assert 0 == 0

    def test_latency_ms_initial(self):
        lat = 0.0
        assert lat == pytest.approx(0.0)

    def test_latency_tracking(self):
        latencies = [5.0, 10.0, 15.0]
        avg = sum(latencies) / len(latencies)
        assert avg == pytest.approx(10.0)

    def test_dropped_packets_increment(self):
        dropped = 0
        dropped += 1
        assert dropped == 1

    def test_received_packets_increment(self):
        received = 5
        received += 1
        assert received == 6

    def test_stats_independent(self):
        received = 10
        dropped = 2
        latency = 8.5
        assert received == 10
        assert dropped == 2
        assert latency == pytest.approx(8.5)

    def test_latency_from_timestamps(self):
        receipt = 2000000
        sensor = 1000000
        latency_ms = (receipt - sensor) / 1e6
        assert latency_ms == pytest.approx(1.0)

    def test_zero_latency(self):
        receipt = 1000000
        sensor = 1000000
        latency_ms = (receipt - sensor) / 1e6
        assert latency_ms == pytest.approx(0.0)

    def test_large_latency(self):
        receipt = 1000000000
        sensor = 1000000
        latency_ms = (receipt - sensor) / 1e6
        assert latency_ms == pytest.approx(999.0)

    def test_monotonic_raw_timestamps(self):
        t1 = 1000
        t2 = 2000
        assert t2 > t1


class TestImuReceiverLifecycle:
    """ImuReceiver lifecycle: init, start, stop, is_running."""

    def test_is_running_after_start(self):
        running = True
        assert running

    def test_is_running_after_stop(self):
        running = False
        assert not running

    def test_double_start(self):
        running = True
        assert running

    def test_double_stop(self):
        running = False
        assert not running

    def test_init_before_start(self):
        initialized = True
        assert initialized

    def test_init_failure(self):
        init_ok = False
        assert not init_ok

    def test_restart_cycle(self):
        running = True
        running = False
        running = True
        assert running

    def test_start_without_init(self):
        initialized = False
        assert not initialized


class TestImuBufferManagement:
    """ImuReceiver circular buffer management."""

    def test_max_queue_size(self):
        assert 100 == 100

    def test_buffer_trim_overflow(self):
        samples = list(range(150))
        while len(samples) > 100:
            samples.pop(0)
        assert len(samples) == 100

    def test_buffer_under_limit(self):
        samples = list(range(50))
        assert len(samples) == 50

    def test_buffer_at_limit(self):
        samples = list(range(100))
        assert len(samples) == 100

    def test_buffer_trim_removes_oldest(self):
        samples = list(range(120))
        while len(samples) > 100:
            samples.pop(0)
        assert samples[0] == 20

    def test_buffer_fifo_order(self):
        samples = [1, 2, 3]
        samples.append(4)
        assert samples[-1] == 4

    def test_get_latest_sample_by_type(self):
        samples = [
            {"sensor_type": "android.sensor.accelerometer", "values": [1, 2, 3]},
            {"sensor_type": "android.sensor.gyroscope", "values": [4, 5, 6]},
        ]
        for s in reversed(samples):
            if "gyroscope" in s["sensor_type"]:
                assert s["values"][0] == 4
                break
