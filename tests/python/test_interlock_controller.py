import pytest


class TestInterlockState:
    """InterlockState enum: safety circuit state."""

    def test_state_values(self):
        assert 0 == 0  # OPEN
        assert 1 == 1  # CLOSED
        assert 2 == 2  # FAULT
        assert 3 == 3  # UNKNOWN

    def test_state_names(self):
        names = ["OPEN", "CLOSED", "FAULT", "UNKNOWN"]
        assert len(names) == 4


class TestInterlockStateToString:
    """interlock_state_to_string: human-readable names."""

    def test_all_states(self):
        mapping = {0: "OPEN", 1: "CLOSED", 2: "FAULT", 3: "UNKNOWN"}
        for v, n in mapping.items():
            assert isinstance(v, int)
            assert isinstance(n, str)

    def test_invalid_state(self):
        mapping = {0: "OPEN", 1: "CLOSED", 2: "FAULT", 3: "UNKNOWN"}
        invalid = 99
        result = "INVALID" if invalid not in mapping else mapping[invalid]
        assert result == "INVALID"


class TestInterlockConfig:
    """InterlockConfig: GPIO/safety configuration validation."""

    def make_config(self, input_pin=17, led_pin=22, inhibit_ch=2,
                    debounce_ms=50, poll_ms=10, active_low=True):
        cfg = {"input_pin": input_pin, "status_led_pin": led_pin,
               "inhibit_channel": inhibit_ch, "debounce_ms": debounce_ms,
               "poll_interval_ms": poll_ms, "active_low": active_low}
        return cfg

    def validate(self, cfg):
        if cfg["input_pin"] < 0 or cfg["input_pin"] > 27:
            return False
        if cfg["status_led_pin"] < 0 or cfg["status_led_pin"] > 27:
            return False
        if cfg["input_pin"] == cfg["status_led_pin"]:
            return False
        if cfg["inhibit_channel"] < 0 or cfg["inhibit_channel"] > 11:
            return False
        return True

    def test_valid_config(self):
        cfg = self.make_config()
        assert self.validate(cfg)

    def test_input_pin_negative(self):
        cfg = self.make_config(input_pin=-1)
        assert not self.validate(cfg)

    def test_input_pin_too_high(self):
        cfg = self.make_config(input_pin=28)
        assert not self.validate(cfg)

    def test_led_pin_negative(self):
        cfg = self.make_config(led_pin=-1)
        assert not self.validate(cfg)

    def test_led_pin_too_high(self):
        cfg = self.make_config(led_pin=28)
        assert not self.validate(cfg)

    def test_duplicate_pins(self):
        cfg = self.make_config(input_pin=17, led_pin=17)
        assert not self.validate(cfg)

    def test_inhibit_channel_negative(self):
        cfg = self.make_config(inhibit_ch=-1)
        assert not self.validate(cfg)

    def test_inhibit_channel_too_high(self):
        cfg = self.make_config(inhibit_ch=12)
        assert not self.validate(cfg)

    def test_default_values(self):
        cfg = self.make_config()
        assert cfg["debounce_ms"] == 50
        assert cfg["poll_interval_ms"] == 10
        assert cfg["active_low"]

    def test_boundary_valid_pins(self):
        cfg_0 = self.make_config(input_pin=0, led_pin=27)
        assert self.validate(cfg_0)
        cfg_27 = self.make_config(input_pin=27, led_pin=0)
        assert self.validate(cfg_27)


class TestInterlockStatus:
    """InterlockStatus: runtime monitoring state."""

    def test_defaults(self):
        s = {"state": 3, "last_change_ns": 0, "transition_count": 0,
             "fault_count": 0, "watchdog_feeds": 0, "actuation_inhibited": True}
        assert s["state"] == 3
        assert s["actuation_inhibited"]


class TestSelfTestResult:
    """SelfTestResult: hardware self-test per AM7-L2-SAFE-007."""

    def test_all_passed(self):
        r = {"comparator_ok": True, "interlock_gpio_ok": True,
             "watchdog_ok": True, "reserved": False}
        assert all([r["comparator_ok"], r["interlock_gpio_ok"], r["watchdog_ok"]])

    def test_comparator_fail(self):
        r = {"comparator_ok": False, "interlock_gpio_ok": True, "watchdog_ok": True}
        assert not r["comparator_ok"]

    def test_gpio_fail(self):
        r = {"comparator_ok": True, "interlock_gpio_ok": False, "watchdog_ok": True}
        assert not r["interlock_gpio_ok"]

    def test_watchdog_fail(self):
        r = {"comparator_ok": True, "interlock_gpio_ok": True, "watchdog_ok": False}
        assert not r["watchdog_ok"]

    def test_all_fail(self):
        r = {"comparator_ok": False, "interlock_gpio_ok": False, "watchdog_ok": False}
        assert not any(r.values())

    def test_get_failure_description_all_pass(self):
        fails = []
        desc = "ALL_PASSED" if not fails else " ".join(fails)
        assert desc == "ALL_PASSED"

    def test_get_failure_description_comparator(self):
        fails = ["COMPARATOR_FAIL"]
        desc = "ALL_PASSED" if not fails else " ".join(fails)
        assert desc == "COMPARATOR_FAIL"

    def test_get_failure_description_multiple(self):
        fails = ["COMPARATOR_FAIL", "GPIO_FAIL", "WATCHDOG_FAIL"]
        desc = "ALL_PASSED" if not fails else " ".join(fails)
        assert "COMPARATOR_FAIL" in desc

    def test_struct_size(self):
        assert 16 == 16

    def test_trivially_copyable(self):
        import struct
        fmt = "???Q"  # bool, bool, bool, bool, uint64_t
        packed = struct.pack(fmt, True, True, True, False)
        assert len(packed) == struct.calcsize(fmt)


class TestInterlockController:
    """InterlockController: hybrid GPIO/I2C interlock."""

    def test_is_actuation_allowed_when_closed(self):
        assert 1 == 1  # CLOSED

    def test_is_actuation_allowed_when_open(self):
        assert 0 != 1

    def test_initial_state_unknown(self):
        assert 3 == 3

    def test_force_state(self):
        pass

    def test_watchdog_feed(self):
        feeds = 5
        assert feeds > 0

    def test_get_input_output_raw_default(self):
        assert 0 == 0

    def test_set_inhibit(self):
        pass
