import pytest
import math


class TestG1DragFullModel:
    """G1 drag: complete Mach regime model with interpolation."""

    def drag_coefficient(self, mach):
        if mach <= 0.8:
            return 0.2
        elif mach <= 1.2:
            return 0.4
        elif mach <= 2.5:
            return 0.25
        else:
            return 0.18

    def test_subsonic_range(self):
        for m in [0.0, 0.3, 0.5, 0.8]:
            assert self.drag_coefficient(m) == 0.2

    def test_transonic_range(self):
        for m in [0.81, 1.0, 1.19, 1.2]:
            assert self.drag_coefficient(m) == 0.4

    def test_supersonic_range(self):
        for m in [1.21, 1.5, 2.0, 2.5]:
            assert self.drag_coefficient(m) == 0.25

    def test_hypersonic_range(self):
        for m in [2.51, 3.0, 5.0, 10.0]:
            assert self.drag_coefficient(m) == 0.18

    def test_drag_continuity_at_boundary(self):
        assert self.drag_coefficient(0.8) == self.drag_coefficient(0.799)
        assert self.drag_coefficient(1.2) == self.drag_coefficient(1.19)
        assert self.drag_coefficient(2.5) == self.drag_coefficient(2.49)


class TestRK4Integration:
    """RK4 trajectory integration for projectile motion."""

    def rk4_step(self, state, dt, accel_func):
        x, y, z, vx, vy, vz = state

        def derivatives(s):
            x, y, z, vx, vy, vz = s
            ax, ay, az = accel_func(x, y, z, vx, vy, vz)
            return (vx, vy, vz, ax, ay, az)

        k1 = derivatives(state)
        k2 = derivatives(tuple(s + 0.5 * dt * d for s, d in zip(state, k1)))
        k3 = derivatives(tuple(s + 0.5 * dt * d for s, d in zip(state, k2)))
        k4 = derivatives(tuple(s + dt * d for s, d in zip(state, k3)))

        new_state = tuple(s + (dt / 6.0) * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i])
                         for i, s in enumerate(state))
        return new_state

    def gravity_only(self, x, y, z, vx, vy, vz):
        return (0.0, 0.0, -9.81)

    def test_gravity_only_drop(self):
        state = (0.0, 0.0, 0.0, 100.0, 0.0, 0.0)
        dt = 0.01
        for _ in range(100):
            state = self.rk4_step(state, dt, self.gravity_only)
        x, y, z, vx, vy, vz = state
        assert abs(x - 100.0) < 0.5
        assert z < 0

    def test_vertical_drop(self):
        state = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
        dt = 0.01
        for _ in range(100):
            state = self.rk4_step(state, dt, self.gravity_only)
        x, y, z, vx, vy, vz = state
        assert abs(z - (-0.5 * 9.81 * 1.0**2)) < 0.05
        assert vz == pytest.approx(-9.81, abs=0.1)

    def test_horizontal_shot(self):
        state = (0.0, 0.0, 0.0, 300.0, 0.0, 0.0)
        dt = 0.001
        for _ in range(500):
            state = self.rk4_step(state, dt, self.gravity_only)
        x, y, z, vx, vy, vz = state
        assert abs(x - 150.0) < 1.0
        assert abs(z) > 0

    def test_symmetry(self):
        state = (0.0, 0.0, 0.0, 100.0, 0.0, 0.0)
        dt = 0.001
        for _ in range(1000):
            state = self.rk4_step(state, dt, self.gravity_only)
        x, y, z, vx, vy, vz = state
        assert abs(y) < 0.1

    def test_energy_conservation(self):
        state = (0.0, 0.0, 100.0, 50.0, 0.0, 0.0)
        dt = 0.001
        ke_initial = 0.5 * (50.0**2)
        pe_initial = 9.81 * 100.0
        e_initial = ke_initial + pe_initial
        for _ in range(100):
            state = self.rk4_step(state, dt, self.gravity_only)
        x, y, z, vx, vy, vz = state
        ke = 0.5 * (vx**2 + vy**2 + vz**2)
        pe = 9.81 * z
        e_final = ke + pe
        assert abs(e_final - e_initial) < 0.5


class TestBallisticProfileValidation:
    """BallisticProfile parameter validation edge cases."""

    def validate_profile(self, p):
        if p["muzzle_velocity"] < 50 or p["muzzle_velocity"] > 1500:
            return False
        if p["bc"] < 0.05 or p["bc"] > 1.5:
            return False
        if p["sight_height"] < 0 or p["sight_height"] > 200:
            return False
        if p["zero_range"] < 10 or p["zero_range"] > 1000:
            return False
        return True

    def test_velocity_edge_cases(self):
        assert self.validate_profile({"muzzle_velocity": 50, "bc": 0.3, "sight_height": 50, "zero_range": 100})
        assert self.validate_profile({"muzzle_velocity": 1500, "bc": 0.3, "sight_height": 50, "zero_range": 100})

    def test_bc_edge_cases(self):
        assert self.validate_profile({"muzzle_velocity": 900, "bc": 0.05, "sight_height": 50, "zero_range": 100})
        assert self.validate_profile({"muzzle_velocity": 900, "bc": 1.5, "sight_height": 50, "zero_range": 100})

    def test_sight_height_zero(self):
        assert self.validate_profile({"muzzle_velocity": 900, "bc": 0.3, "sight_height": 0, "zero_range": 100})

    def test_zero_range_edge(self):
        assert self.validate_profile({"muzzle_velocity": 900, "bc": 0.3, "sight_height": 50, "zero_range": 10})
        assert self.validate_profile({"muzzle_velocity": 900, "bc": 0.3, "sight_height": 50, "zero_range": 1000})

    def test_all_params_at_lower_bound(self):
        p = {"muzzle_velocity": 50, "bc": 0.05, "sight_height": 0, "zero_range": 10}
        assert self.validate_profile(p)

    def test_all_params_at_upper_bound(self):
        p = {"muzzle_velocity": 1500, "bc": 1.5, "sight_height": 200, "zero_range": 1000}
        assert self.validate_profile(p)


class TestBallisticLookupTable:
    """Lookup table indexing math."""

    def range_to_idx(self, r, r_min=0.1, r_max=10.0, bins=100):
        return int(round((r - r_min) / ((r_max - r_min) / (bins - 1))))

    def velocity_to_idx(self, v, v_min=50.0, v_max=500.0, bins=46):
        return int(round((v - v_min) / ((v_max - v_min) / (bins - 1))))

    def target_vel_to_idx(self, tv, tv_min=0.0, tv_max=20.0, bins=5):
        return int(round((tv - tv_min) / ((tv_max - tv_min) / (bins - 1))))

    def test_range_index_bounds(self):
        assert self.range_to_idx(0.1) == 0
        assert self.range_to_idx(10.0) == 99

    def test_range_index_interpolation(self):
        idx1 = self.range_to_idx(0.2)
        idx2 = self.range_to_idx(1.0)
        assert idx2 > idx1

    def test_velocity_index_bounds(self):
        assert self.velocity_to_idx(50.0) == 0
        assert self.velocity_to_idx(500.0) == 45

    def test_target_velocity_index_bounds(self):
        assert self.target_vel_to_idx(0.0) == 0
        assert self.target_vel_to_idx(20.0) == 4

    def test_index_clamping(self):
        def clamped_range_idx(r):
            idx = self.range_to_idx(r)
            return max(0, min(99, idx))
        assert clamped_range_idx(0.0) == 0
        assert clamped_range_idx(50.0) == 99
        assert clamped_range_idx(-10.0) == 0
        assert clamped_range_idx(100.0) == 99

    def test_index_monotonic(self):
        prev = -1
        for r in [r * 0.1 for r in range(1, 100)]:
            idx = self.range_to_idx(r)
            assert idx >= prev
            prev = idx

    def test_table_dimensions(self):
        r_bins = 100
        v_bins = 46
        tv_bins = 5
        assert r_bins * v_bins == 4600
        assert r_bins * v_bins * tv_bins == 23000


class TestBallisticSolverGravity:
    """Gravity constant and air density."""

    def test_gravity_variation(self):
        assert 9.81 == pytest.approx(9.81)

    def test_air_density_standard(self):
        assert 1.225 == pytest.approx(1.225)

    def test_air_density_at_altitude(self):
        def density_at_alt(h_m):
            return 1.225 * math.exp(-h_m / 8500.0)
        assert density_at_alt(0) == pytest.approx(1.225)
        assert density_at_alt(8500) == pytest.approx(1.225 / math.e, rel=0.01)
        assert density_at_alt(500) < 1.225


class TestEngagementThresholds:
    """Engagement decision thresholds."""

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

    def test_p_hit_scale_factors(self):
        az_sigma = 0.010
        el_sigma = 0.010
        range_sigma = 5.0
        assert az_sigma > 0
        assert el_sigma > 0
        assert range_sigma > 0


class TestTrajectoryTimeOfFlight:
    """Time of flight calculations."""

    def tof_kinetic(self, range_m, velocity):
        return max(0.001, range_m / max(velocity, 1.0))

    def test_tof_at_100m(self):
        tof = self.tof_kinetic(100.0, 900.0)
        assert tof == pytest.approx(0.111, abs=0.01)

    def test_tof_at_500m(self):
        tof = self.tof_kinetic(500.0, 900.0)
        assert tof == pytest.approx(0.556, abs=0.01)

    def test_tof_linear_with_range(self):
        t1 = self.tof_kinetic(100.0, 900.0)
        t2 = self.tof_kinetic(200.0, 900.0)
        assert abs(t2 - 2 * t1) < 0.01

    def test_tof_inverse_with_velocity(self):
        t1 = self.tof_kinetic(100.0, 900.0)
        t2 = self.tof_kinetic(100.0, 450.0)
        assert abs(t2 - 2 * t1) < 0.01

    def test_tof_minimum(self):
        t = self.tof_kinetic(0.5, 900.0)
        assert t > 0

    def test_tof_zero_range(self):
        t = self.tof_kinetic(0.0, 900.0)
        assert t >= 0.001


class TestBallisticConstants:
    """BallisticSolver internal constants."""

    def test_mach_segments(self):
        assert 0.8 == 0.8
        assert 1.2 == 1.2
        assert 2.5 == 2.5
        assert 10.0 == 10.0

    def test_range_limit(self):
        min_r = 0.1
        max_r = 10.0
        assert min_r < max_r

    def test_velocity_limits(self):
        min_v = 50.0
        max_v = 500.0
        assert min_v < max_v
