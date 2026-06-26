import os
import sys
import socket
import subprocess
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
AURO_RELINK_DIR = PROJECT_ROOT / "aurore_link"
CONFIG_DIR = PROJECT_ROOT / "config"
BUILD_DIR = PROJECT_ROOT / "build-rpi"


def pytest_configure(config):
    sys.path.insert(0, str(AURO_RELINK_DIR))
    config.addinivalue_line("markers", "network: marks tests that require a running aurore binary or open network ports")


@pytest.fixture(scope="session")
def project_root() -> Path:
    return PROJECT_ROOT


@pytest.fixture(scope="session")
def config_dir() -> Path:
    return CONFIG_DIR


@pytest.fixture(scope="session")
def build_dir() -> Path:
    return BUILD_DIR


@pytest.fixture(scope="session")
def aurore_binary() -> Path:
    path = BUILD_DIR / "aurore"
    if not path.exists():
        pytest.skip(f"aurore binary not found at {path}")
    return path


@pytest.fixture(scope="session")
def config_path(config_dir: Path) -> Path:
    path = config_dir / "config.json"
    if not path.exists():
        pytest.skip(f"config.json not found at {path}")
    return path


@pytest.fixture(scope="session")
def calibration_path(config_dir: Path) -> Path:
    path = config_dir / "calibration.json"
    if not path.exists():
        pytest.skip(f"calibration.json not found at {path}")
    return path


@pytest.fixture(scope="session")
def aurore_running() -> bool:
    result = subprocess.run(
        ["pgrep", "-x", "aurore"],
        capture_output=True,
        timeout=5,
    )
    if result.returncode != 0:
        pytest.skip("aurore process is not running")
    return True


@pytest.fixture(scope="session")
def telemetry_port() -> int:
    return 9000


@pytest.fixture(scope="session")
def command_port() -> int:
    return 9002


@pytest.fixture(scope="session")
def aurore_ports_open(telemetry_port: int, command_port: int) -> bool:
    for port in [telemetry_port, command_port]:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        try:
            result = sock.connect_ex(("127.0.0.1", port))
            if result != 0:
                pytest.skip(f"port {port} is not open")
        finally:
            sock.close()
    return True
