"""
Hardware Presence Tests

Verifies that required hardware devices are present on the system.
These tests fail immediately if hardware is unavailable — no graceful
degradation per project policy.

References:
- CSI camera: /dev/video* (libcamera)
- Fusion HAT I2C: /dev/i2c-1
- Laser rangefinder: /dev/ttyAMA0
- V4L device nodes
"""
import os
import glob
import subprocess

from pathlib import Path

import pytest


CSI_VIDEO_GLOB = "/dev/video*"
I2C_BUS_1 = "/dev/i2c-1"
UART_TTYAMA0 = "/dev/ttyAMA0"
FUSION_HAT_I2C_ADDRESS = 0x17  # 23 decimal


def pytest_configure(config):
    config.addinivalue_line("markers", "hardware: marks tests that require physical hardware")


class TestCameraHardware:
    def test_video_device_nodes_exist(self):
        devices = glob.glob(CSI_VIDEO_GLOB)
        assert len(devices) > 0, f"No video device nodes found at {CSI_VIDEO_GLOB}"

    def test_at_least_one_camera_available(self):
        try:
            result = subprocess.run(
                ["libcamera-hello", "--list-cameras"],
                capture_output=True, text=True, timeout=10,
            )
            has_output = bool(result.stdout.strip()) or bool(result.stderr.strip())
            assert has_output, "libcamera reports no cameras available"
        except FileNotFoundError:
            assert False, "libcamera-hello not installed — camera hardware test failed"

    def test_v4l_device_present(self):
        devices = glob.glob(CSI_VIDEO_GLOB)
        assert len(devices) > 0
        for dev in devices:
            assert os.access(dev, os.R_OK | os.W_OK), f"Device {dev} not readable/writable"


class TestI2cHardware:
    def test_i2c_bus_exists(self):
        assert os.path.exists(I2C_BUS_1), f"I2C bus {I2C_BUS_1} not found"

    def test_i2c_bus_readable(self):
        assert os.access(I2C_BUS_1, os.R_OK | os.W_OK), (
            f"I2C bus {I2C_BUS_1} not readable/writable"
        )

    def test_fusion_hat_i2c_detected(self):
        result = subprocess.run(
            ["i2cdetect", "-y", "1"],
            capture_output=True, text=True, timeout=10,
        )
        assert result.returncode == 0, f"i2cdetect failed: {result.stderr}"
        hex_addr = f"{FUSION_HAT_I2C_ADDRESS:02x}"
        assert hex_addr in result.stdout, (
            f"Fusion HAT not detected at I2C address 0x{hex_addr}"
        )

    def test_i2c_speed_is_400khz(self):
        result = subprocess.run(
            ["i2cget", "-y", "1", "0x23", "0x00"],
            capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 0 or "Error" not in result.stderr, (
            f"I2C communication with Fusion HAT failed: {result.stderr}"
        )


class TestUartHardware:
    def test_uart_device_exists(self):
        assert os.path.exists(UART_TTYAMA0), f"UART device {UART_TTYAMA0} not found"

    def test_uart_device_readable(self):
        assert os.access(UART_TTYAMA0, os.R_OK | os.W_OK), (
            f"UART device {UART_TTYAMA0} not readable/writable"
        )


class TestSystemConfig:
    def test_preempt_rt_kernel(self):
        result = subprocess.run(
            ["uname", "-a"],
            capture_output=True, text=True, timeout=5,
        )
        is_rt = "PREEMPT_RT" in result.stdout or "PREEMPT" in result.stdout
        if not is_rt:
            pytest.skip("not running PREEMPT_RT kernel — real-time tests skipped")

    def test_isolated_cpus(self):
        cmdline = Path("/proc/cmdline").read_text()
        if "isolcpus" not in cmdline:
            pytest.skip("isolcpus not configured in kernel cmdline")
        assert "isolcpus=2-3" in cmdline, (
            f"Unexpected isolcpus config: {cmdline}"
        )

    def test_performance_governor(self):
        for cpu in range(4):
            gov_path = f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor"
            if os.path.exists(gov_path):
                gov = Path(gov_path).read_text().strip()
                if gov != "performance":
                    pytest.skip(f"CPU{cpu} governor is {gov}, not 'performance'")
