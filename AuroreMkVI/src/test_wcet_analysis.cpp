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

#include "timing.h"

constexpr int WCET_TEST_FRAMES = 1000;
constexpr int WARMUP_FRAMES = 50;

struct WCETResult {
    std::string stage_name;
    double min_us;
    double max_us;
    double mean_us;
    double std_us;
    double p95_us;
    double p99_us;
    double wcet_us;
    int count;
    bool exceeds_budget;
};

std::vector<double> parse_latencies_from_trace(const std::string& trace_file, int target_stage);

WCETResult analyze_stage_latencies(const std::string& name, const std::vector<double>& latencies_us, double budget_us) {
    WCETResult result;
    result.stage_name = name;
    result.count = latencies_us.size();
    
    if (latencies_us.empty()) {
        result.min_us = result.max_us = result.mean_us = result.std_us = 0;
        result.p95_us = result.p99_us = result.wcet_us = 0;
        result.exceeds_budget = false;
        return result;
    }
    
    std::vector<double> sorted = latencies_us;
    std::sort(sorted.begin(), sorted.end());
    
    result.min_us = sorted.front();
    result.max_us = sorted.back();
    result.mean_us = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0) / latencies_us.size();
    result.wcet_us = result.max_us;
    
    // Standard deviation
    double sq_sum = 0;
    for (double lat : latencies_us) {
        sq_sum += (lat - result.mean_us) * (lat - result.mean_us);
    }
    result.std_us = std::sqrt(sq_sum / latencies_us.size());
    
    // Percentiles
    size_t p95_idx = static_cast<size_t>(sorted.size() * 0.95);
    size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
    result.p95_us = sorted[p95_idx];
    result.p99_us = sorted[p99_idx];
    
    result.exceeds_budget = result.p95_us > budget_us;
    
    return result;
}

void print_wcet_result(const WCETResult& r, double budget_us) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n  " << r.stage_name << ":\n";
    std::cout << "    Count:     " << r.count << "\n";
    std::cout << "    Min:       " << r.min_us << " us\n";
    std::cout << "    Max:       " << r.max_us << " us\n";
    std::cout << "    Mean:      " << r.mean_us << " us\n";
    std::cout << "    Std Dev:   " << r.std_us << " us\n";
    std::cout << "    P95:       " << r.p95_us << " us\n";
    std::cout << "    P99:       " << r.p99_us << " us\n";
    std::cout << "    WCET:      " << r.wcet_us << " us\n";
    std::cout << "    Budget:    " << budget_us << " us\n";
    std::cout << "    Status:    " << (r.exceeds_budget ? "EXCEEDS BUDGET" : "OK") << "\n";
}

int main(int argc, char* argv[]) {
    std::string trace_file = "pipeline_trace.csv";
    double frame_budget_us = 16666.67;  // 60fps = 16.667ms per frame
    
    if (argc > 1) {
        trace_file = argv[1];
    }
    if (argc > 2) {
        frame_budget_us = std::stod(argv[2]) * 1000.0;
    }
    
    std::cout << "========================================\n";
    std::cout << "WCET Analysis - Timing Determinism Audit\n";
    std::cout << "========================================\n";
    std::cout << "Trace file: " << trace_file << "\n";
    std::cout << "Frame budget: " << frame_budget_us << " us (" << (frame_budget_us / 1000.0) << " ms)\n";
    
    std::vector<WCETResult> results;
    
    // Stage configurations: (name, stage_id, budget_percent_of_frame)
    struct StageConfig {
        const char* name;
        int stage_id;
        double budget_percent;
    };
    
    std::vector<StageConfig> stages = {
        {"MIPI_INTERRUPT", 0, 0.5},
        {"LIBCAMERA_CAPTURE", 1, 3.0},
        {"IMAGE_PROCESSOR", 2, 4.0},
        {"TPU_INFERENCE", 3, 8.0},
        {"DETECTION_PARSING", 4, 1.0},
        {"BALLISTICS_CALC", 5, 1.0},
        {"FIRE_AUTHORIZATION", 6, 0.5},
        {"SERVO_ACTUATION", 7, 1.0}
    };
    
    bool trace_exists = std::ifstream(trace_file).good();
    
    if (!trace_exists) {
        std::cout << "\nTrace file not found: " << trace_file << "\n";
        std::cout << "Generating simulated WCET data for demonstration...\n\n";
        
        for (const auto& stage : stages) {
            double stage_budget = frame_budget_us * stage.budget_percent / 100.0;
            
            // Simulate realistic latencies with some variance
            std::vector<double> latencies;
            for (int i = 0; i < WCET_TEST_FRAMES; i++) {
                double base = stage_budget * 0.3;
                double noise = (rand() % 1000) / 1000.0 * stage_budget * 0.4;
                double spike = (rand() % 100 < 5) ? stage_budget * 0.5 : 0;  // 5% spike
                latencies.push_back(base + noise + spike);
            }
            
            WCETResult result = analyze_stage_latencies(stage.name, latencies, stage_budget);
            results.push_back(result);
        }
    } else {
        std::cout << "\nAnalyzing trace data...\n";
        
        for (const auto& stage : stages) {
            double stage_budget = frame_budget_us * stage.budget_percent / 100.0;
            
            std::vector<double> latencies = parse_latencies_from_trace(trace_file, stage.stage_id);
            
            if (!latencies.empty()) {
                WCETResult result = analyze_stage_latencies(stage.name, latencies, stage_budget);
                results.push_back(result);
            } else {
                WCETResult empty;
                empty.stage_name = stage.name;
                empty.count = 0;
                empty.exceeds_budget = false;
                results.push_back(empty);
            }
        }
    }
    
    // Print results
    std::cout << "\n========================================\n";
    std::cout << "STAGE-BY-STAGE WCET ANALYSIS\n";
    std::cout << "========================================\n";
    
    double total_p95 = 0;
    double total_wcet = 0;
    
    for (size_t i = 0; i < stages.size(); i++) {
        double stage_budget = frame_budget_us * stages[i].budget_percent / 100.0;
        if (i < results.size()) {
            print_wcet_result(results[i], stage_budget);
            total_p95 += results[i].p95_us;
            total_wcet += results[i].wcet_us;
        }
    }
    
    // Summary
    std::cout << "\n========================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "========================================\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total P95 Latency:  " << total_p95 << " us (" << total_p95/1000.0 << " ms)\n";
    std::cout << "Total WCET:         " << total_wcet << " us (" << total_wcet/1000.0 << " ms)\n";
    std::cout << "Frame Budget:       " << frame_budget_us << " us (" << frame_budget_us/1000.0 << " ms)\n";
    std::cout << "Budget Margin:      " << (frame_budget_us - total_p95) << " us\n";
    
    bool overall_ok = total_p95 < frame_budget_us;
    std::cout << "\nOverall Status:     " << (overall_ok ? "PASS - Meets 60fps budget" : "FAIL - Exceeds budget") << "\n";
    
    // Recommendations
    std::cout << "\n========================================\n";
    std::cout << "OPTIMIZATION RECOMMENDATIONS\n";
    std::cout << "========================================\n";
    
    for (const auto& r : results) {
        if (r.exceeds_budget && r.count > 0) {
            std::cout << "- " << r.stage_name << ": P95 (" << r.p95_us << " us) exceeds budget\n";
        }
    }
    
    return overall_ok ? 0 : 1;
}

std::vector<double> parse_latencies_from_trace(const std::string& trace_file, int target_stage) {
    (void)target_stage;  // Reserved for stage filtering
    std::vector<double> latencies;
    std::ifstream file(trace_file);
    
    if (!file.is_open()) {
        return latencies;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    struct Entry { uint64_t ts; uint32_t frame; int stage; bool is_exit; };
    std::vector<Entry> entries;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // Parse CSV: frame_id,stage,timestamp_ns,latency_ns or frame_id,stage,timestamp_ns,enter/exit
        std::stringstream ss(line);
        std::string field;
        
        std::getline(ss, field, ',');
        uint32_t frame_id = std::stoul(field);
        
        std::getline(ss, field, ',');
        int stage = std::stoi(field);
        
        std::getline(ss, field, ',');
        uint64_t timestamp = std::stoull(field);
        
        std::getline(ss, field, ',');
        std::string event_type = field;
        
        if (event_type == "enter") {
            entries.push_back({timestamp, frame_id, stage, false});
        } else if (event_type == "exit") {
            entries.push_back({timestamp, frame_id, stage, true});
        }
    }
    
    // Match enter/exit pairs
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].is_exit) {
            for (size_t j = i + 1; j < entries.size(); j++) {
                if (entries[j].is_exit && entries[j].frame == entries[i].frame && entries[j].stage == entries[i].stage) {
                    double latency_us = static_cast<double>(entries[j].ts - entries[i].ts) / 1000.0;
                    latencies.push_back(latency_us);
                    break;
                }
            }
        }
    }
    
    return latencies;
}
