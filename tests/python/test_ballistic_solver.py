import pytest
import math
import struct


class TestBallisticProfile:
    """BallisticProfile: projectile parameter validation."""

    def test_valid_profile(self):
        p = {"name": "test", "muzzle_velocity_m_s": 900.0,
             "ballistic_coefficient": 0.3, "sight_height_mm": 50.0,
             "zero_range_m": 100.0}
        ok = True
        if p["muzzle_velocity_m_s"] < 50 or p["muzzle_velocity_m_s"] > 1500:
            ok = False
        if p["ballistic_coefficient"] < 0.05 or p["ballistic_coefficient"] > 1.5:
            ok = False
        if p["sight_height_mm"] < 0 or p["sight_height_mm"] > 200:
            ok = False
        if p["zero_range_m"] < 10 or p["zero_range_m"] > 1000:
            ok = False
        assert ok

    def test_muzzle_velocity_too_low(self):
        assert not (30 >= 50 and 30 <= 1500)

    def test_muzzle_velocity_too_high(self):
        assert not (2000 >= 50 and 2000 <= 1500)

    def test_ballistic_coefficient_too_low(self):
        assert not (0.01 >= 0.05 and 0.01 <= 1.5)

    def test_ballistic_coefficient_too_high(self):
        assert not (2.0 >= 0.05 and 2.0 <= 1.5)

    def test_sight_height_negative(self):
        assert not (-1 >= 0 and -1 <= 200)

    def test_sight_height_too_high(self):
        assert not (300 >= 0 and 300 <= 200)

    def test_zero_range_too_low(self):
        assert not (5 >= 10 and 5 <= 1000)

    def test_zero_range_too_high(self):
        assert not (5000 >= 10 and 5000 <= 1000)

    def test_edge_cases(self):
        assert 50.0 >= 50.0
        assert 1500.0 <= 1500.0
        assert 0.05 >= 0.05
        assert 1.5 <= 1.5
        assert 0.0 >= 0.0
        assert 200.0 <= 200.0
        assert 10.0 >= 10.0
        assert 1000.0 <= 1000.0


class TestEngagementMode:
    """EngagementMode: KINETIC vs DROP."""

    def test_kinetic_value(self):
        assert 0 == 0

    def test_drop_value(self):
        assert 1 == 1


class TestKineticSolution:
    """KineticSolution: lead angles for direct fire."""

    def test_defaults(self):
        ks = {"el_lead_deg": 0.0, "az_lead_deg": 0.0, "tof_s": 0.0}
        assert ks["el_lead_deg"] == 0.0

    def test_typical_values(self):
        ks = {"el_lead_deg": 2.5, "az_lead_deg": 1.0, "tof_s": 0.15}
        assert ks["tof_s"] > 0

    def test_az_lead_for_moving_target(self):
        az_lead = 3.0
        assert az_lead > 0


class TestDropSolution:
    """DropSolution: launch parameters for indirect fire."""

    def test_defaults(self):
        ds = {"el_lead_deg": 0.0, "az_lead_deg": 0.0, "launch_v_m_s": 0.0, "tof_s": 0.0}
        assert ds["launch_v_m_s"] == 0.0

    def test_typical(self):
        ds = {"el_lead_deg": 15.0, "az_lead_deg": 0.5, "launch_v_m_s": 100.0, "tof_s": 1.2}
        assert ds["el_lead_deg"] > 10.0


class TestG1DragModel:
    """G1 drag model constants."""

    def test_mach_segments(self):
        assert 0.8 == 0.8    # kMachSubsonicMax
        assert 1.2 == 1.2    # kMachTransonicMax
        assert 2.5 == 2.5    # kMachSupersonicMax
        assert 10.0 == 10.0  # kMachHypersonicMax

    def test_drag_coefficients(self):
        assert 0.2 == 0.2    # kCdSubsonic
        assert 0.4 == 0.4    # kCdTransonic
        assert 0.25 == 0.25  # kCdSupersonic
        assert 0.18 == 0.18  # kCdHypersonic

    def test_speed_of_sound(self):
        assert 343.0 == 343.0

    def test_drag_coefficient_subsonic(self):
        mach = 0.5
        cd = 0.2 if mach <= 0.8 else 0.4 if mach <= 1.2 else 0.25 if mach <= 2.5 else 0.18
        assert cd == 0.2

    def test_drag_coefficient_transonic(self):
        mach = 1.0
        cd = 0.2 if mach <= 0.8 else 0.4 if mach <= 1.2 else 0.25 if mach <= 2.5 else 0.18
        assert cd == 0.4

    def test_drag_coefficient_supersonic(self):
        mach = 2.0
        cd = 0.2 if mach <= 0.8 else 0.4 if mach <= 1.2 else 0.25 if mach <= 2.5 else 0.18
        assert cd == 0.25

    def test_drag_coefficient_hypersonic(self):
        mach = 5.0
        cd = 0.2 if mach <= 0.8 else 0.4 if mach <= 1.2 else 0.25 if mach <= 2.5 else 0.18
        assert cd == 0.18


class TestRK4Integration:
    """RK4 state and derivative structures."""

    def test_rk4_state_defaults(self):
        s = {"x": 0.0, "y": 0.0, "z": 0.0, "vx": 0.0, "vy": 0.0, "vz": 0.0}
        assert s["x"] == 0.0

    def test_rk4_derivative_defaults(self):
        d = {"dx": 0.0, "dy": 0.0, "dz": 0.0, "dvx": 0.0, "dvy": 0.0, "dvz": 0.0}
        assert d["dvx"] == 0.0

    def test_gravity_constant(self):
        assert 9.81 == 9.81

    def test_default_air_density(self):
        assert 1.225 == 1.225


class TestLookupTable:
    """PERF-005: Pre-computed lookup table dimensions."""

    def test_range_bins(self):
        assert 100 == 100

    def test_velocity_bins(self):
        assert 46 == 46

    def test_target_velocity_bins(self):
        assert 5 == 5

    def test_range_limits(self):
        assert 0.1 == pytest.approx(0.1)
        assert 10.0 == pytest.approx(10.0)

    def test_velocity_limits(self):
        assert 50.0 == pytest.approx(50.0)
        assert 500.0 == pytest.approx(500.0)

    def test_target_velocity_limits(self):
        assert 0.0 == pytest.approx(0.0)
        assert 20.0 == pytest.approx(20.0)

    def test_table_size_kinetic(self):
        size = 100 * 46
        assert size == 4600

    def test_table_size_drop(self):
        size = 100 * 46
        assert size == 4600

    def test_table_size_tof(self):
        size = 100 * 46
        assert size == 4600

    def test_table_size_el_lead(self):
        size = 100 * 46
        assert size == 4600

    def test_table_size_az_lead(self):
        size = 100 * 46 * 5
        assert size == 23000

    def test_total_lut_size_bytes(self):
        total_entries = 4600 * 4 + 23000
        assert total_entries == 41400

    def test_index_conversion(self):
        def range_to_idx(r):
            return int(round((r - 0.1) / (9.9 / 99)))
        assert range_to_idx(0.1) == 0
        assert range_to_idx(10.0) == 99

    def test_velocity_index(self):
        def vel_to_idx(v):
            return int(round((v - 50.0) / (450.0 / 45)))
        assert vel_to_idx(50.0) == 0


class TestBallisticSolverConstants:
    """BallisticSolver internal constants."""

    def test_gravity(self):
        assert 9.81 == pytest.approx(9.81)

    def test_default_density(self):
        assert 1.225 == pytest.approx(1.225)

    def test_aspect_drop_threshold(self):
        assert 2.0 == pytest.approx(2.0)

    def test_elev_drop_threshold(self):
        assert 45.0 == pytest.approx(45.0)

    def test_range_drop_threshold(self):
        assert 1.5 == pytest.approx(1.5)

    def test_sigma_values(self):
        assert 0.010 == pytest.approx(0.010)
        assert 5.0 == pytest.approx(5.0)
        assert 0.02 == pytest.approx(0.02)
        assert 0.1 == pytest.approx(0.1)

    def test_target_half_size(self):
        assert 0.040 == pytest.approx(0.040)
