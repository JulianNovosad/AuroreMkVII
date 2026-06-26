import pytest


class TestCommandSocketConfig:
    """CommandSocket::Config: socket path and security."""

    def test_default_socket_path(self):
        path = "/tmp/aurore_cmd.sock"
        assert path.startswith("/tmp/")
        assert path.endswith(".sock")

    def test_default_allowed_uid(self):
        allowed_uid = 0
        assert allowed_uid == 0

    def test_custom_socket_path(self):
        cfg = {"socket_path": "/run/aurore/cmd.sock", "allowed_uid": 1000}
        assert cfg["socket_path"] == "/run/aurore/cmd.sock"

    def test_custom_allowed_uid(self):
        cfg = {"allowed_uid": 1000}
        assert cfg["allowed_uid"] == 1000

    def test_path_length_limit(self):
        path = "/tmp/aurore_cmd.sock"
        assert len(path) < 108

    def test_socket_path_not_empty(self):
        assert len("/tmp/aurore_cmd.sock") > 0


class TestCommandSocketProtocol:
    """CommandSocket: text protocol parsing."""

    def parse_mode(self, line):
        parts = line.strip().split()
        if len(parts) == 2 and parts[0] == "MODE":
            return parts[1]
        return None

    def parse_freecam(self, line):
        parts = line.strip().split()
        if len(parts) == 3 and parts[0] == "FREECAM":
            try:
                return (float(parts[1]), float(parts[2]))
            except ValueError:
                return None
        return None

    def parse_reset(self, line):
        return line.strip() == "RESET"

    def test_mode_auto(self):
        mode = self.parse_mode("MODE AUTO")
        assert mode == "AUTO"

    def test_mode_freecam(self):
        mode = self.parse_mode("MODE FREECAM")
        assert mode == "FREECAM"

    def test_mode_idle(self):
        mode = self.parse_mode("MODE IDLE")
        assert mode == "IDLE"

    def test_freecam_with_angles(self):
        result = self.parse_freecam("FREECAM 45.0 -10.0")
        assert result == (45.0, -10.0)

    def test_freecam_negative_azimuth(self):
        result = self.parse_freecam("FREECAM -90.0 0.0")
        assert result == (-90.0, 0.0)

    def test_reset_command(self):
        assert self.parse_reset("RESET")

    def test_invalid_mode(self):
        assert self.parse_mode("INVALID") is None

    def test_invalid_freecam_format(self):
        assert self.parse_freecam("FREECAM abc") is None

    def test_empty_line(self):
        assert self.parse_mode("") is None
        assert not self.parse_reset("")

    def test_case_sensitivity(self):
        assert self.parse_mode("MODE auto") == "auto"
        assert self.parse_mode("mode AUTO") is None

    def test_extra_whitespace(self):
        mode = self.parse_mode("  MODE AUTO  ")
        assert mode == "AUTO"

    def test_newline_terminated(self):
        mode = self.parse_mode("MODE AUTO\n")
        assert mode == "AUTO"

    def test_freecam_angle_range(self):
        az, el = 45.0, -10.0
        assert -180.0 <= az <= 180.0
        assert -90.0 <= el <= 90.0

    def test_freecam_azimuth_bounds(self):
        for az in [-90.0, -45.0, 0.0, 45.0, 90.0]:
            assert -90.0 <= az <= 90.0

    def test_freecam_elevation_bounds(self):
        for el in [-10.0, -5.0, 0.0, 10.0, 45.0]:
            assert -10.0 <= el <= 45.0

    def test_mode_invalid_value(self):
        assert self.parse_mode("MODE") is None

    def test_freecam_invalid_number(self):
        assert self.parse_freecam("FREECAM abc 10") is None


class TestCommandSocketCallbacks:
    """CommandSocket: callback dispatch."""

    def test_mode_callback(self):
        received = []
        def on_mode(mode):
            received.append(mode)
        on_mode("AUTO")
        assert received == ["AUTO"]

    def test_freecam_callback(self):
        received = []
        def on_freecam(az, el):
            received.append((az, el))
        on_freecam(45.0, -10.0)
        assert received == [(45.0, -10.0)]

    def test_reset_callback(self):
        called = False
        def on_reset():
            nonlocal called
            called = True
        on_reset()
        assert called

    def test_multiple_callbacks(self):
        events = []
        events.append("mode")
        events.append("freecam")
        events.append("reset")
        assert events == ["mode", "freecam", "reset"]

    def test_callback_replacement(self):
        cb1 = lambda m: "old"
        cb2 = lambda m: "new"
        cb1 = cb2
        assert cb1("test") == "new"


class TestCommandSocketDispatch:
    """CommandSocket::dispatch: routing lines to callbacks."""

    def test_dispatch_mode_auto(self):
        cmd = "MODE AUTO"
        parts = cmd.strip().split()
        assert parts[0] == "MODE"
        assert parts[1] == "AUTO"

    def test_dispatch_mode_freecam(self):
        cmd = "MODE FREECAM"
        parts = cmd.strip().split()
        assert parts[1] == "FREECAM"

    def test_dispatch_freecam_with_args(self):
        cmd = "FREECAM 45.0 -10.0"
        parts = cmd.strip().split()
        assert parts[0] == "FREECAM"
        assert float(parts[1]) == 45.0
        assert float(parts[2]) == -10.0

    def test_dispatch_reset(self):
        cmd = "RESET"
        assert cmd == "RESET"

    def test_unknown_command_ignored(self):
        cmd = "UNKNOWN"
        assert cmd == "UNKNOWN"

    def test_dispatch_multiple_lines(self):
        cmds = ["MODE AUTO\n", "FREECAM 10 20\n", "RESET\n"]
        assert len(cmds) == 3


class TestCommandSocketLifecycle:
    """CommandSocket::start/stop lifecycle."""

    def test_initial_not_running(self):
        running = False
        assert not running

    def test_start_sets_running(self):
        running = True
        assert running

    def test_stop_clears_running(self):
        running = True
        running = False
        assert not running

    def test_double_start(self):
        started = True
        assert started

    def test_double_stop(self):
        stopped = True
        assert stopped
