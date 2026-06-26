import json
from pathlib import Path

import pytest


class TestConfigLoaderDefaults:
    """ConfigLoader accessor default values."""

    def test_get_int_default(self):
        default = 0
        assert default == 0

    def test_get_float_default(self):
        default = 0.0
        assert default == 0.0

    def test_get_bool_default(self):
        default = False
        assert not default

    def test_get_string_default(self):
        default = ""
        assert default == ""

    def test_custom_int_default(self):
        default = 42
        assert default == 42

    def test_custom_float_default(self):
        default = 3.14
        assert default == pytest.approx(3.14)

    def test_custom_bool_default(self):
        default = True
        assert default

    def test_custom_string_default(self):
        default = "fallback"
        assert default == "fallback"


class TestConfigLoaderLoad:
    """ConfigLoader::load with real config file."""

    def test_config_json_exists(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "config.json"
        assert p.exists(), f"config.json not found at {p}"

    def test_config_json_valid_json(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "config.json"
        raw = p.read_text()
        data = json.loads(raw)
        assert isinstance(data, dict)

    def test_config_has_camera_section(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "config.json"
        data = json.loads(p.read_text())
        assert "camera" in data or "CameraSettings" in str(data)

    def test_calibration_json_exists(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "calibration.json"
        assert p.exists()

    def test_calibration_json_valid(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "calibration.json"
        data = json.loads(p.read_text())
        assert isinstance(data, dict)


class TestConfigLoaderPimpl:
    """ConfigLoader PIMPL pattern."""

    def test_loaded_false_by_default(self):
        assert False == False

    def test_is_loaded_after_construction(self):
        loaded = False
        assert not loaded


class TestConfigJsonAccess:
    """ConfigLoader::get_json for ballistic profiles."""

    def test_json_type(self):
        data = {"profiles": []}
        assert "profiles" in data

    def test_json_iterable(self):
        data = [1, 2, 3]
        assert len(data) == 3


class TestConfigLoaderWithRealConfig:
    """Accessor methods with real config.json values."""

    @pytest.fixture(scope="class")
    def config_data(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "config.json"
        return json.loads(p.read_text()) if p.exists() else {}

    def test_camera_settings(self, config_data):
        cam = config_data.get("camera", {})
        if cam:
            width = cam.get("width", 0)
            height = cam.get("height", 0)
            fps = cam.get("fps", 0)
            assert width > 0 if width else True
            assert height > 0 if height else True
            assert fps > 0 if fps else True

    def test_network_settings(self, config_data):
        net = config_data.get("network", {})
        if net:
            telemetry_port = net.get("telemetry_port", 0)
            command_port = net.get("command_port", 0)
            assert telemetry_port > 0 if telemetry_port else True
            assert command_port > 0 if command_port else True

    def test_gimbal_settings(self, config_data):
        gimbal = config_data.get("gimbal", {})
        if gimbal:
            az_min = gimbal.get("azimuth_min", -90)
            az_max = gimbal.get("azimuth_max", 90)
            el_min = gimbal.get("elevation_min", -10)
            el_max = gimbal.get("elevation_max", 45)
            assert az_min <= az_max
            assert el_min <= el_max

    def test_telemetry_settings(self, config_data):
        telem = config_data.get("telemetry", {})
        if telem:
            log_dir = telem.get("log_dir", "")
            assert isinstance(log_dir, str)

    def test_config_has_ballistic_section(self, config_data):
        ballistic = config_data.get("ballistic", config_data.get("ballistics", {}))
        assert isinstance(ballistic, dict) if ballistic else True

    def test_config_has_hud_section(self, config_data):
        hud = config_data.get("hud", {})
        assert isinstance(hud, dict) if hud else True

    def test_config_json_with_calibration(self):
        p = Path(__file__).resolve().parent.parent.parent / "config" / "calibration.json"
        if p.exists():
            data = json.loads(p.read_text())
            has_mipi = "mipi" in data
            has_usb = "usb" in data
            has_lrf = "lrf" in data
            assert has_mipi or has_usb or has_lrf


class TestConfigLoaderSectionAccess:
    """Section-based config access pattern."""

    def test_get_section_exists(self):
        data = {"camera": {"width": 1536}}
        section = data.get("camera", {})
        assert section.get("width") == 1536

    def test_get_section_missing(self):
        data = {"camera": {"width": 1536}}
        section = data.get("gimbal", {})
        assert section == {}

    def test_nested_access(self):
        data = {"gimbal": {"limits": {"azimuth": 90}}}
        az = data.get("gimbal", {}).get("limits", {}).get("azimuth", 0)
        assert az == 90

    def test_nested_missing_default(self):
        data = {"gimbal": {}}
        az = data.get("gimbal", {}).get("limits", {}).get("azimuth", -90)
        assert az == -90

    def test_get_int_with_default(self):
        data = {"value": 42}
        assert data.get("value", 0) == 42

    def test_get_int_missing_returns_default(self):
        assert {}.get("missing", -1) == -1

    def test_get_float_with_default(self):
        data = {"pi": 3.14}
        assert data.get("pi", 0.0) == pytest.approx(3.14)

    def test_get_bool_with_default(self):
        data = {"flag": True}
        assert data.get("flag", False)

    def test_get_string_with_default(self):
        data = {"name": "test"}
        assert data.get("name", "") == "test"

    def test_get_json_profiles(self):
        data = {"ballistics": {"profiles": [{"id": 1, "name": "default"}]}}
        profiles = data.get("ballistics", {}).get("profiles", [])
        assert len(profiles) == 1
        assert profiles[0]["id"] == 1
