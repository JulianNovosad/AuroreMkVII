import json
import re
from pathlib import Path

import pytest

REQUIRED_TOP_LEVEL_KEYS = {"system", "camera", "gimbal", "safety", "network"}
OPTIONAL_TOP_LEVEL_KEYS = {"interlock", "ballistics", "logging", "lrf", "detector", "calibration"}

SYSTEM_SUBKEYS = {"frame_rate_hz", "control_loop_period_ms", "safety_monitor_period_ms"}
CAMERA_SUBKEYS = {"width", "height", "fps", "format", "buffer_count"}
GIMBAL_SUBKEYS = {"elevation", "azimuth", "i2c"}

ELEVATION_KEYS = {"min_deg", "max_deg"}
AZIMUTH_KEYS = {"min_deg", "max_deg"}
I2C_KEYS = {"bus", "address"}

SAFETY_SUBKEYS = {"vision_deadline_ns", "actuation_deadline_ns"}
WATCHDOG_KEYS = {"enabled", "timeout_ms", "kick_interval_ms"}
FAULT_KEYS = {"camera_timeout_ms", "gimbal_timeout_ms", "temperature_critical_c"}


def load_json(path: Path):
    with open(path) as f:
        return json.load(f)


class TestConfigStructure:
    def test_loads_valid_json(self, config_path: Path):
        cfg = load_json(config_path)
        assert isinstance(cfg, dict)

    def test_required_top_level_keys(self, config_path: Path):
        cfg = load_json(config_path)
        missing = REQUIRED_TOP_LEVEL_KEYS - set(cfg.keys())
        assert not missing, f"Missing required top-level keys: {missing}"

    def test_no_unknown_top_level_keys(self, config_path: Path):
        cfg = load_json(config_path)
        known = REQUIRED_TOP_LEVEL_KEYS | OPTIONAL_TOP_LEVEL_KEYS
        extra = set(cfg.keys()) - known - {"_comment", "_version"}
        assert not extra, f"Unexpected top-level keys: {extra}"

    def test_system_section(self, config_path: Path):
        cfg = load_json(config_path)["system"]
        missing = SYSTEM_SUBKEYS - set(cfg.keys())
        assert not missing, f"Missing system keys: {missing}"
        assert isinstance(cfg["frame_rate_hz"], (int, float))
        assert cfg["frame_rate_hz"] > 0

    def test_camera_section(self, config_path: Path):
        cfg = load_json(config_path)["camera"]
        missing = CAMERA_SUBKEYS - set(cfg.keys())
        assert not missing, f"Missing camera keys: {missing}"
        assert cfg["format"] in ("RAW10", "MJPEG", "YUYV", "BGR888")
        assert cfg["width"] >= 320
        assert cfg["height"] >= 240

    def test_gimbal_section(self, config_path: Path):
        cfg = load_json(config_path)["gimbal"]
        missing = GIMBAL_SUBKEYS - set(cfg.keys())
        assert not missing, f"Missing gimbal keys: {missing}"

    def test_gimbal_elevation_limits(self, config_path: Path):
        elev = load_json(config_path)["gimbal"]["elevation"]
        missing = ELEVATION_KEYS - set(elev.keys())
        assert not missing, f"Missing elevation keys: {missing}"
        assert elev["min_deg"] >= -90.0
        assert elev["max_deg"] <= 90.0
        assert elev["min_deg"] < elev["max_deg"]

    def test_gimbal_azimuth_limits(self, config_path: Path):
        az = load_json(config_path)["gimbal"]["azimuth"]
        missing = AZIMUTH_KEYS - set(az.keys())
        assert not missing, f"Missing azimuth keys: {missing}"
        assert az["min_deg"] >= -180.0
        assert az["max_deg"] <= 180.0
        assert az["min_deg"] < az["max_deg"]

    def test_gimbal_i2c_config(self, config_path: Path):
        i2c = load_json(config_path)["gimbal"]["i2c"]
        missing = I2C_KEYS - set(i2c.keys())
        assert not missing, f"Missing I2C keys: {missing}"
        assert isinstance(i2c["bus"], int)
        assert i2c["bus"] >= 0
        assert isinstance(i2c["address"], int)
        assert 0 < i2c["address"] < 128

    def test_safety_vision_deadline(self, config_path: Path):
        safety = load_json(config_path)["safety"]
        assert safety["vision_deadline_ns"] > 0
        assert safety["vision_deadline_ns"] <= 50000000

    def test_safety_watchdog(self, config_path: Path):
        wd = load_json(config_path)["safety"].get("watchdog", {})
        missing = WATCHDOG_KEYS - set(wd.keys())
        assert not missing, f"Missing watchdog keys: {missing}"
        assert wd["timeout_ms"] >= wd["kick_interval_ms"]

    def test_safety_fault_timeouts(self, config_path: Path):
        faults = load_json(config_path)["safety"].get("faults", {})
        missing = FAULT_KEYS - set(faults.keys())
        assert not missing, f"Missing fault keys: {missing}"
        assert faults["temperature_critical_c"] <= 100


class TestConfigValues:
    def test_version_string(self, config_path: Path):
        cfg = load_json(config_path)
        ver = cfg.get("_version", "")
        assert re.match(r"^\d+\.\d+\.\d+$", ver), f"Invalid version: {ver}"

    def test_camera_fps_within_range(self, config_path: Path):
        fps = load_json(config_path)["camera"]["fps"]
        assert 1 <= fps <= 240

    def test_camera_resolution_ratios(self, config_path: Path):
        cam = load_json(config_path)["camera"]
        expected = 1536 / 864
        actual = cam["width"] / cam["height"]
        assert abs(actual - expected) < 0.01, (
            f"Resolution ratio {actual:.3f} != {expected:.3f} (16:9)"
        )

    def test_network_ports_in_range(self, config_path: Path):
        net = load_json(config_path).get("network", {})
        link = net.get("aurore_link", {})
        tp = link.get("telemetry_port", 0)
        cp = link.get("command_port", 0)
        for port in (tp, cp):
            assert 1024 <= port <= 65535

    def test_scheduler_priorities_in_range(self, config_path: Path):
        sched = load_json(config_path)["system"].get("sched_priority", {})
        for name, prio in sched.items():
            assert 1 <= prio <= 99, f"{name} priority {prio} out of range (1-99)"

    def test_cpu_affinities_in_range(self, config_path: Path):
        aff = load_json(config_path)["system"].get("cpu_affinity", {})
        for name, cpu in aff.items():
            assert 0 <= cpu <= 3, f"{name} CPU affinity {cpu} out of range (0-3)"

    def test_ballistics_profiles_valid(self, config_path: Path):
        ballistics = load_json(config_path).get("ballistics", {})
        mv = ballistics.get("muzzle_velocity_mps", 0)
        assert mv > 0, "muzzle_velocity_mps must be > 0"
        profiles = ballistics.get("profiles", [])
        for p in profiles:
            assert p.get("ballistic_coefficient", 0) > 0
            assert p.get("sight_height_mm", 0) > 0

    def test_interlock_default_state(self, config_path: Path):
        il = load_json(config_path).get("interlock", {})
        state = il.get("default_state", "")
        assert state in ("inhibit", "enable"), f"Invalid interlock state: {state}"


class TestCalibrationConfig:
    def test_loads_valid_json(self, calibration_path: Path):
        cfg = json.loads(calibration_path.read_text())
        assert isinstance(cfg, dict)

    def test_has_servo_section(self, calibration_path: Path):
        cfg = json.loads(calibration_path.read_text())
        assert "servo" in cfg
        servo = cfg["servo"]
        assert "pan_channel" in servo
        assert "tilt_channel" in servo

    def test_camera_offset_exists(self, calibration_path: Path):
        cfg = json.loads(calibration_path.read_text())
        cam = cfg.get("cameras", {})
        assert "pixel_offset_x" in cam
        assert "pixel_offset_y" in cam
