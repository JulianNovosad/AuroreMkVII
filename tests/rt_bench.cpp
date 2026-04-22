#include "aurore/ballistic_solver.hpp"
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <iomanip>

int main() {
    aurore::BallisticSolver solver;
    const int duration_sec = 10;
    const int target_hz = 1000; // 1ms steps
    const int total_iterations = duration_sec * target_hz;
    
    std::vector<double> latencies;
    latencies.reserve(total_iterations);

    std::cout << "Starting 10s Real-Time Stress Test (1kHz)..." << std::endl;

    for (int i = 0; i < total_iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Heavy query: p_hit for moving target at varying range (0.5m to 2.0m sweep)
        float range = 0.5f + static_cast<float>(i % 1500) / 1000.0f;
        solver.solve(range, -5.0f, 0.0f, 60.0f, 1.5f);

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::nano> elapsed = end - start;
        latencies.push_back(elapsed.count());

        // Busy-wait to maintain precise 1ms interval
        auto next_tick = start + std::chrono::milliseconds(1);
        while (std::chrono::high_resolution_clock::now() < next_tick);
    }

    std::sort(latencies.begin(), latencies.end());
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) /
                 static_cast<double>(latencies.size());
    
    std::cout << "\n--- Aurore Mk VII Ballistics Real-Time Report ---" << std::endl;
    std::cout << "Average Latency:  " << std::fixed << std::setprecision(2) << avg << " ns" << std::endl;
    std::cout << "95th Percentile:  " << latencies[total_iterations * 0.95] << " ns" << std::endl;
    std::cout << "99th Percentile:  " << latencies[total_iterations * 0.99] << " ns" << std::endl;
    std::cout << "Worst-Case (Max): " << latencies.back() << " ns" << std::endl;
    
    if (latencies.back() > 5000000.0) {
        std::cout << "WARNING: DEADLINE VIOLATION (>5ms) DETECTED!" << std::endl;
    } else {
        std::cout << "STATUS: ALL DEADLINES MET (Hard Real-Time Compliant)" << std::endl;
    }
    return 0;
}
