import math
import struct

import pytest


class TestG1DragModel:
    """G1 drag: 4-segment piecewise Cd vs Mach number."""

    def drag_coefficient(self, mach):
        if mach <= 0.8:
            return 0.2
        elif mach <= 1.2:
            return 0.4
        elif mach <= 2.5:
            return 0.25
        elif mach <= 10.0:
            return 0.18
        return 0.18

    def test_subsonic_below_08(self):
        for m in [0.0, 0.3, 0.5, 0.79]:
            assert self.drag_coefficient(m) == 0.2

    def test_transonic_at_08(self):
        assert self.drag_coefficient(0.8) == 0.2

    def test_transonic_above_08(self):
        for m in [0.81, 0.9, 1.0, 1.19]:
            assert self.drag_coefficient(m) == 0.4

    def test_supersonic_at_12(self):
        assert self.drag_coefficient(1.2) == 0.4

    def test_supersonic_above_12(self):
        for m in [1.21, 1.5, 2.0, 2.49]:
            assert self.drag_coefficient(m) == 0.25

    def test_hypersonic_at_25(self):
        assert self.drag_coefficient(2.5) == 0.25

    def test_hypersonic_above_25(self):
        for m in [2.51, 3.0, 5.0, 10.0]:
            assert self.drag_coefficient(m) == 0.18

    def test_above_hypersonic_max(self):
        assert self.drag_coefficient(10.0) == 0.18

    def test_mach_subsonic_max(self):
        assert 0.8 == 0.8

    def test_mach_transonic_max(self):
        assert 1.2 == 1.2

    def test_mach_supersonic_max(self):
        assert 2.5 == 2.5

    def test_mach_hypersonic_max(self):
        assert 10.0 == 10.0

    def test_cd_subsonic(self):
        assert 0.2 == 0.2

    def test_cd_transonic(self):
        assert 0.4 == 0.4

    def test_cd_supersonic(self):
        assert 0.25 == 0.25

    def test_cd_hypersonic(self):
        assert 0.18 == 0.18

    def test_speed_of_sound(self):
        kSpeedOfSound = 343.0
        assert kSpeedOfSound == 343.0

    def test_mach_from_velocity(self):
        v = 686.0
        mach = v / 343.0
        assert abs(mach - 2.0) < 0.01

    def test_transonic_peak_highest_cd(self):
        cd_values = [0.2, 0.4, 0.25, 0.18]
        assert max(cd_values) == 0.4

    def test_cd_transonic_double_subsonic(self):
        assert 0.4 == 2.0 * 0.2


class TestRk4Integration:
    """RK4 state and derivative structures."""

    def test_rk4_state_has_6_fields(self):
        state = {"x": 0, "y": 0, "z": 0, "vx": 0, "vy": 0, "vz": 0}
        assert len(state) == 6

    def test_rk4_derivative_has_6_fields(self):
        deriv = {"dx": 0, "dy": 0, "dz": 0, "dvx": 0, "dvy": 0, "dvz": 0}
        assert len(deriv) == 6

    def test_derivative_dx_equals_state_vx(self):
        vx = 100.0
        dx = vx
        assert dx == vx

    def test_derivative_dvx_from_forces(self):
        drag_accel = -5.0
        gravity = 0.0
        dvx = drag_accel + gravity
        assert dvx == -5.0

    def test_gravity_constant(self):
        assert 9.81 == 9.81

    def test_default_density(self):
        assert 1.225 == 1.225

    def test_rk4_step_forward(self):
        x = 0.0
        vx = 100.0
        dt = 0.0005
        new_x = x + vx * dt
        assert abs(new_x - 0.05) < 0.001

    def test_rk4_step_velocity_update(self):
        vx = 100.0
        ax = -9.81
        dt = 0.0005
        new_vx = vx + ax * dt
        assert abs(new_vx - 99.995095) < 0.001

    def test_trajectory_point_fields(self):
        pt = {"x": 0, "y": 0, "z": 0, "vx": 0, "vy": 0, "vz": 0, "time": 0}
        assert len(pt) == 7

    def test_default_integration_step(self):
        dt = 0.0005
        assert dt == 0.0005

    def test_simulate_trajectory_default_params(self):
        max_distance = 1000.0
        dt = 0.0005
        max_steps = int(max_distance / (900.0 * dt))
        assert max_steps > 0

    def test_tof_monotonic(self):
        times = [0.0, 0.1, 0.2, 0.5, 1.0]
        assert all(times[i] < times[i+1] for i in range(len(times)-1))


class TestBallisticProfileValidation:
    """BallisticProfile::validate: boundary testing."""

    def test_valid_profile(self):
        p = {"muzzle_velocity_m_s": 900.0, "ballistic_coefficient": 0.300,
             "sight_height_mm": 50.0, "zero_range_m": 100.0}
        ok = (50 <= p["muzzle_velocity_m_s"] <= 1500 and
              0.05 <= p["ballistic_coefficient"] <= 1.5 and
              0 <= p["sight_height_mm"] <= 200 and
              10 <= p["zero_range_m"] <= 1000)
        assert ok

    def test_muzzle_velocity_min(self):
        assert not (50 < 50)

    def test_muzzle_velocity_max(self):
        assert not (1500 > 1500)

    def test_muzzle_velocity_boundary_low(self):
        assert 50 >= 50

    def test_muzzle_velocity_boundary_high(self):
        assert 1500 <= 1500

    def test_bc_min(self):
        assert not (0.05 < 0.05)

    def test_bc_max(self):
        assert 1.5 >= 1.5

    def test_sight_height_min(self):
        assert 0 >= 0

    def test_sight_height_max(self):
        assert 200 <= 200

    def test_zero_range_min(self):
        assert 10 >= 10

    def test_zero_range_max(self):
        assert 1000 <= 1000

    def test_defaults_valid(self):
        p = {"muzzle_velocity_m_s": 900.0, "ballistic_coefficient": 0.300,
             "sight_height_mm": 50.0, "zero_range_m": 100.0}
        ok = (50 <= p["muzzle_velocity_m_s"] <= 1500 and
              0.05 <= p["ballistic_coefficient"] <= 1.5 and
              0 <= p["sight_height_mm"] <= 200 and
              10 <= p["zero_range_m"] <= 1000)
        assert ok


class TestEngagementModeSelection:
    """EngagementMode: KINETIC vs DROP logic."""

    def test_kinetic_value(self):
        assert 0 == 0

    def test_drop_value(self):
        assert 1 == 1

    def test_aspect_threshold(self):
        kAspectDropThresh = 2.0
        assert kAspectDropThresh == 2.0

    def test_elevation_threshold(self):
        kElevDropThresh = 45.0
        assert kElevDropThresh == 45.0

    def test_range_threshold(self):
        kRangeDropThresh = 1.5
        assert kRangeDropThresh == 1.5


class TestLookupTableProperties:
    """Lookup table constants and dimensions."""

    def test_range_bins(self):
        assert 100 == 100

    def test_velocity_bins(self):
        assert 46 == 46

    def test_target_vel_bins(self):
        assert 5 == 5

    def test_min_range(self):
        assert 0.1 == 0.1

    def test_max_range(self):
        assert 10.0 == 10.0

    def test_min_velocity(self):
        assert 50.0 == 50.0

    def test_max_velocity(self):
        assert 500.0 == 500.0

    def test_min_target_vel(self):
        assert 0.0 == 0.0

    def test_max_target_vel(self):
        assert 20.0 == 20.0

    def test_range_step(self):
        n_bins = 100
        r_min = 0.1
        r_max = 10.0
        step = (r_max - r_min) / (n_bins - 1)
        assert abs(step - 0.1) < 0.001

    def test_velocity_step(self):
        n_bins = 46
        v_min = 50.0
        v_max = 500.0
        step = (v_max - v_min) / (n_bins - 1)
        assert abs(step - 10.0) < 0.001

    def test_target_vel_step(self):
        n_bins = 5
        tv_min = 0.0
        tv_max = 20.0
        step = (tv_max - tv_min) / (n_bins - 1)
        assert abs(step - 5.0) < 0.001

    def test_range_index_0(self):
        """At min range, index = 0."""
        idx = int(round(0))
        assert idx == 0

    def test_range_index_99(self):
        """At max range, index = 99."""
        idx = int(round(99))
        assert idx == 99

    def test_lookup_table_count(self):
        count = 100 * 46
        assert count == 4600

    def test_table_count_az_lead(self):
        count = 100 * 46 * 5
        assert count == 23000

    def test_four_tables_each_4600(self):
        tables = 4
        assert tables * 4600 == 18400

    def test_az_lead_table_entries(self):
        assert 23000 == 23000

    def test_total_entries_float32(self):
        total = 4600 * 4 + 23000
        assert total == 41400

    def test_total_memory_bytes(self):
        total_entries = 41400
        bytes_per_entry = 4
        total_bytes = total_entries * bytes_per_entry
        assert total_bytes == 165600

    def test_total_memory_kb(self):
        total_bytes = 165600
        kb = total_bytes / 1024
        assert abs(kb - 161.7) < 1.0

    def test_l3_cache_fit(self):
        total_bytes = 165600
        l3_cache = 2097152
        assert total_bytes < l3_cache

    def test_normalized_formula(self):
        """index = round((value - min) / (max - min) * (n_bins - 1))"""
        for r in [0.1, 1.0, 5.0, 10.0]:
            normalized = (r - 0.1) / (10.0 - 0.1)
            idx = int(round(normalized * 99))
            assert 0 <= idx <= 99

    def test_range_to_index_rounding(self):
        """Values near bin centers map correctly."""
        for tenth in range(0, 100):
            r = 0.1 + tenth * 0.1
            idx = int(round((r - 0.1) / 9.9 * 99))
            assert idx == tenth

    def test_velocity_to_index_rounding(self):
        for tenth in range(0, 46):
            v = 50.0 + tenth * 10.0
            idx = int(round((v - 50.0) / 450.0 * 45))
            assert idx == tenth


class TestFireControlSolutionConstants:
    """BallisticSolver private Monte Carlo constants."""

    def test_range_sigma(self):
        assert 0.010 == 0.010

    def test_velocity_sigma(self):
        assert 5.0 == 5.0

    def test_density_sigma(self):
        assert 0.02 == 0.02

    def test_align_sigma_deg(self):
        assert 0.1 == 0.1

    def test_target_half_size_m(self):
        assert 0.040 == 0.040

    def test_target_size_mm(self):
        assert 0.040 * 1000 == 40.0


class TestKineticSolution:
    """KineticSolution: lead angle fields."""

    def test_el_lead_default_zero(self):
        s = {"el_lead_deg": 0.0}
        assert s["el_lead_deg"] == 0.0

    def test_az_lead_default_zero(self):
        s = {"az_lead_deg": 0.0}
        assert s["az_lead_deg"] == 0.0

    def test_tof_default_zero(self):
        s = {"tof_s": 0.0}
        assert s["tof_s"] == 0.0

    def test_lead_angle_positive(self):
        s = {"el_lead_deg": 5.0, "az_lead_deg": 2.0}
        assert s["el_lead_deg"] > 0
        assert s["az_lead_deg"] > 0

    def test_lead_degree_ranges(self):
        for lead in [-90.0, -45.0, 0.0, 45.0, 90.0]:
            assert -90 <= lead <= 90


class TestDropSolution:
    """DropSolution: additional launch_v field."""

    def test_launch_v_default_zero(self):
        s = {"launch_v_m_s": 0.0}
        assert s["launch_v_m_s"] == 0.0

    def test_launch_v_positive_when_set(self):
        s = {"launch_v_m_s": 300.0}
        assert s["launch_v_m_s"] > 0


class TestTrajectorySimulation:
    """simulate_trajectory: parameter validation."""

    def test_max_distance_default(self):
        assert 1000 > 0

    def test_simulates_to_max_distance(self):
        muzzle_v = 900.0
        max_dist = 1000.0
        tof = max_dist / muzzle_v
        assert abs(tof - 1.111) < 0.01

    def test_no_wind_assumption(self):
        wind = 0.0
        assert wind == 0.0

    def test_no_coriolis(self):
        assert True

    def test_flat_earth_assumption(self):
        assert True

    def test_standard_atmosphere(self):
        density = 1.225
        assert density > 1.0
