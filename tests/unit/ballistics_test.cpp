#include "aurore/ballistic_solver.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace aurore;

void test_kinetic_mode_tof_formula() {
    BallisticSolver solver;
    auto sol = solver.solve_kinetic(1.0f, 0.f, 700.f);
    assert(sol.has_value());
    assert(std::abs(sol->el_lead_deg) < 0.5f);
    assert(sol->tof_s < 0.01f);
    std::cout << "PASS: kinetic mode tof < 10ms at 1m/700m/s\n";
}

void test_drop_mode_requires_valid_arc() {
    BallisticSolver solver;
    auto sol = solver.solve_drop(1.5f, -0.5f);
    assert(sol.has_value());
    assert(sol->el_lead_deg > 0.f);
    assert(sol->launch_v_m_s > 0.f);
    std::cout << "PASS: drop mode finds upward arc for 1.5m/-0.5m target\n";
}

void test_drop_mode_impossible_geometry() {
    BallisticSolver solver;
    auto sol = solver.solve_drop(3.0f, 2.5f);
    std::cout << "PASS: drop mode handles upward target gracefully\n";
}

void test_monte_carlo_p_hit_perfect_inputs() {
    BallisticSolver solver;
    FireControlSolution perfect;
    perfect.range_m = 1.0f;
    perfect.el_lead_deg = 0.f;
    perfect.az_lead_deg = 0.f;
    perfect.velocity_m_s = 700.f;
    perfect.kinetic_mode = true;

    float p = solver.monte_carlo_p_hit(perfect, 50);
    assert(p >= 0.f && p <= 1.f);
    std::cout << "PASS: monte carlo returns [0,1] result for " << p << "\n";
}

void test_mode_selection_kinetic_for_shallow_target() {
    BallisticSolver solver;
    auto mode = solver.select_mode(1.5f, 10.f, 1.0f);
    assert(mode == EngagementMode::KINETIC);
    std::cout << "PASS: KINETIC mode selected for shallow elevation\n";
}

void test_mode_selection_drop_for_top_down() {
    BallisticSolver solver;
    auto mode = solver.select_mode(1.2f, 5.f, 2.5f);
    assert(mode == EngagementMode::DROP);
    std::cout << "PASS: DROP mode selected for top-down aspect\n";
}

int main() {
    test_kinetic_mode_tof_formula();
    test_drop_mode_requires_valid_arc();
    test_drop_mode_impossible_geometry();
    test_monte_carlo_p_hit_perfect_inputs();
    test_mode_selection_kinetic_for_shallow_target();
    test_mode_selection_drop_for_top_down();
    std::cout << "\nAll ballistics tests passed.\n";
    return 0;
}
