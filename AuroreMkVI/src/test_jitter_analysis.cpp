#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <deque>

#include "timing.h"

constexpr size_t JITTER_WINDOW_SIZE = 100;

struct JitterAnalysis {
    std::string name;
    double mean_us;
    double std_us;
    double min_us;
    double max_us;
    double jitter_us;
    double p99_us;
    bool meets_target;
};

// Forward declarations
std::vector<double> parse_stage_latencies(const std::string& trace_file, int target_stage);
std::vector<double> parse_end_to_end_latencies(const std::string& trace_file);

JitterAnalysis analyze_jitter(const std::string& name, const std::vector<double>& intervals_us, double target_jitter_us) {
    JitterAnalysis result;
    result.name = name;
    
    if (intervals_us.size() < 2) {
        result.mean_us = result.std_us = result.min_us = result.max_us = result.jitter_us = result.p99_us = 0;
        result.meets_target = true;
        return result;
    }
    
    // Calculate intervals between consecutive measurements
    std::vector<double> deltas;
    for (size_t i = 1; i < intervals_us.size(); i++) {
        double delta = std::abs(intervals_us[i] - intervals_us[i-1]);
        deltas.push_back(delta);
    }
    
    std::sort(deltas.begin(), deltas.end());
    
    result.mean_us = std::accumulate(deltas.begin(), deltas.end(), 0.0) / deltas.size();
    result.min_us = deltas.front();
    result.max_us = deltas.back();
    
    // Jitter = standard deviation of intervals
    double sq_sum = 0;
    for (double d : deltas) {
        sq_sum += (d - result.mean_us) * (d - result.mean_us);
    }
    result.jitter_us = std::sqrt(sq_sum / deltas.size());
    result.std_us = result.jitter_us;
    
    // P99 of deltas
    size_t p99_idx = static_cast<size_t>(deltas.size() * 0.99);
    result.p99_us = deltas[p99_idx];
    
    result.meets_target = result.jitter_us < target_jitter_us;
    
    return result;
}

void print_jitter_result(const JitterAnalysis& r, double target) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n  " << r.name << ":\n";
    std::cout << "    Mean interval: " << r.mean_us << " us\n";
    std::cout << "    Min delta:     " << r.min_us << " us\n";
    std::cout << "    Max delta:     " << r.max_us << " us\n";
    std::cout << "    Jitter (std):  " << r.jitter_us << " us\n";
    std::cout << "    P99 delta:     " << r.p99_us << " us\n";
    std::cout << "    Target jitter: " << target << " us\n";
    std::cout << "    Status:        " << (r.meets_target ? "PASS" : "FAIL - exceeds target") << "\n";
}

int main(int argc, char* argv[]) {
    std::string trace_file = "pipeline_trace.csv";
    double target_jitter_us = 1000.0;  // 1ms target for capture-to-inference path
    
    if (argc > 1) {
        trace_file = argv[1];
    }
    if (argc > 2) {
        target_jitter_us = std::stod(argv[2]) * 1000.0;
    }
    
    std::cout << "========================================\n";
    std::cout << "Jitter Analysis - Determinism Audit\n";
    std::cout << "========================================\n";
    std::cout << "Trace file: " << trace_file << "\n";
    std::cout << "Target jitter: " << target_jitter_us << " us (" << (target_jitter_us/1000.0) << " ms)\n";
    
    std::vector<JitterAnalysis> results;
    
    struct StageConfig {
        const char* name;
        int stage_id;
    };
    
    std::vector<StageConfig> stages = {
        {"MIPI Interrupt", 0},
        {"Libcamera Capture", 1},
        {"Image Processor", 2},
        {"TPU Inference", 3},
        {"Detection Parsing", 4},
        {"Ballistics Calc", 5},
        {"Fire Authorization", 6},
        {"Servo Actuation", 7}
    };
    
    bool trace_exists = std::ifstream(trace_file).good();
    
    if (!trace_exists) {
        std::cout << "\nTrace file not found: " << trace_file << "\n";
        std::cout << "Generating simulated jitter data for demonstration...\n\n";
        
        for (const auto& stage : stages) {
            std::vector<double> latencies;
            double base = 2000 + (rand() % 2000);  // 2-4ms base
            for (int i = 0; i < 1000; i++) {
                double noise = (rand() % 1000 - 500) / 100.0;  // +/- 5us noise
                double spike = (rand() % 100 < 2) ? (rand() % 2000) : 0;  // 2% spike
                latencies.push_back(base + noise + spike);
            }
            
            JitterAnalysis result = analyze_jitter(stage.name, latencies, target_jitter_us);
            results.push_back(result);
        }
    } else {
        std::cout << "\nAnalyzing trace data for jitter...\n";
        
        for (const auto& stage : stages) {
            std::vector<double> latencies = parse_stage_latencies(trace_file, stage.stage_id);
            if (!latencies.empty()) {
                JitterAnalysis result = analyze_jitter(stage.name, latencies, target_jitter_us);
                results.push_back(result);
            } else {
                JitterAnalysis empty;
                empty.name = stage.name;
                empty.meets_target = true;
                results.push_back(empty);
            }
        }
    }
    
    // Print results
    std::cout << "\n========================================\n";
    std::cout << "JITTER ANALYSIS BY STAGE\n";
    std::cout << "========================================\n";
    
    bool all_pass = true;
    for (const auto& r : results) {
        print_jitter_result(r, target_jitter_us);
        if (!r.meets_target && r.mean_us > 0) all_pass = false;
    }
    
    // End-to-end jitter
    std::cout << "\n========================================\n";
    std::cout << "END-TO-END JITTER (MIPI to Servo)\n";
    std::cout << "========================================\n";
    
    std::vector<double> end_to_end = parse_end_to_end_latencies(trace_file);
    if (!end_to_end.empty()) {
        JitterAnalysis e2e = analyze_jitter("End-to-End", end_to_end, target_jitter_us * 5);  // Relaxed for E2E
        print_jitter_result(e2e, target_jitter_us * 5);
    } else {
        // Simulated E2E jitter
        std::vector<double> simulated_e2e;
        double base = 16000;  // ~16ms frame time
        for (int i = 0; i < 1000; i++) {
            double noise = (rand() % 2000 - 1000) / 100.0;
            simulated_e2e.push_back(base + noise);
        }
        JitterAnalysis e2e = analyze_jitter("End-to-End (Simulated)", simulated_e2e, target_jitter_us * 5);
        print_jitter_result(e2e, target_jitter_us * 5);
    }
    
    std::cout << "\n========================================\n";
    std::cout << "JITTER SOURCES IDENTIFICATION\n";
    std::cout << "========================================\n";
    
    std::cout << "\nPotential jitter sources:\n";
    std::cout << "  1. Memory allocation in ImageProcessor (resize operations)\n";
    std::cout << "  2. TPU inference variability (model execution time)\n";
    std::cout << "  3. Queue wait times (producer-consumer synchronization)\n";
    std::cout << "  4. Cache misses on buffer recycling\n";
    std::cout << "  5. Thread scheduling preemption\n";
    
    std::cout << "\n========================================\n";
    std::cout << "RECOMMENDATIONS\n";
    std::cout << "========================================\n";
    
    for (const auto& r : results) {
        if (!r.meets_target && r.mean_us > 0) {
            std::cout << "- " << r.name << ": Investigate memory allocation patterns\n";
            std::cout << "  Consider pre-allocating buffers and using memory pools\n";
        }
    }
    
    return all_pass ? 0 : 1;
}

std::vector<double> parse_stage_latencies(const std::string& trace_file, int target_stage) {
    std::vector<double> latencies;
    std::ifstream file(trace_file);
    
    if (!file.is_open()) return latencies;
    
    std::string line;
    std::getline(file, line);
    
    struct Entry { uint64_t ts; uint32_t frame; int stage; bool is_exit; };
    std::vector<Entry> entries;
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string field;
        
        std::getline(ss, field, ',');
        uint32_t frame_id = std::stoul(field);
        
        std::getline(ss, field, ',');
        int stage = std::stoi(field);
        
        std::getline(ss, field, ',');
        uint64_t ts = std::stoull(field);
        
        std::getline(ss, field, ',');
        bool is_exit = (field == "exit");
        
        if (stage == target_stage) {
            entries.push_back({ts, frame_id, stage, is_exit});
        }
    }
    
    // Match enter/exit pairs
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].is_exit) {
            for (size_t j = i + 1; j < entries.size(); j++) {
                if (entries[j].is_exit && entries[j].frame == entries[i].frame) {
                    double lat_us = static_cast<double>(entries[j].ts - entries[i].ts) / 1000.0;
                    latencies.push_back(lat_us);
                    break;
                }
            }
        }
    }
    
    return latencies;
}

std::vector<double> parse_end_to_end_latencies(const std::string& trace_file) {
    std::vector<double> latencies;
    std::ifstream file(trace_file);
    
    if (!file.is_open()) return latencies;
    
    std::string line;
    std::getline(file, line);
    
    struct Entry { uint64_t ts; uint32_t frame; int stage; };
    std::vector<Entry> mipi_entries;
    std::vector<Entry> servo_entries;
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string field;
        
        std::getline(ss, field, ',');
        uint32_t frame_id = std::stoul(field);
        
        std::getline(ss, field, ',');
        int stage = std::stoi(field);
        
        std::getline(ss, field, ',');
        uint64_t ts = std::stoull(field);
        
        std::getline(ss, field, ',');
        std::string event = field;
        
        if (stage == 0 && event == "enter") {  // MIPI
            mipi_entries.push_back({ts, frame_id, stage});
        } else if (stage == 7 && event == "enter") {  // Servo enter
            servo_entries.push_back({ts, frame_id, stage});
        }
    }
    
    // Match frames
    for (const auto& mipi : mipi_entries) {
        for (const auto& servo : servo_entries) {
            if (servo.frame == mipi.frame) {
                double lat_us = static_cast<double>(servo.ts - mipi.ts) / 1000.0;
                latencies.push_back(lat_us);
                break;
            }
        }
    }
    
    return latencies;
}
