#!/bin/bash
# jitter_monitor.sh - Real-time jitter monitoring tool
#
# Monitors control loop jitter and reports timing violations.
# Requires root privileges for SCHED_FIFO.
#
# Usage: sudo ./scripts/jitter_monitor.sh [options]
#
# Options:
#   --duration=N   Monitoring duration in seconds (default: 60)
#   --period=Ns    Loop period in nanoseconds (default: 8333333 for 120Hz)

set -e

# Check root privileges
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root for SCHED_FIFO scheduling."
    echo "Usage: sudo $0 [options]"
    exit 1
fi

DURATION=60
PERIOD_NS=8333333  # 120Hz

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --duration=*)
            DURATION="${1#*=}"
            shift
            ;;
        --period=*)
            PERIOD_NS="${1#*=}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=== Aurore MkVII Jitter Monitor ==="
echo "Duration: ${DURATION}s"
echo "Period:   ${PERIOD_NS}ns ($(echo "scale=3; $PERIOD_NS / 1000000" | bc)ms)"
echo ""

# Create monitoring program
cat > /tmp/jitter_monitor.cpp << 'CPP_CODE'
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>

std::atomic<bool> g_running(true);

void signal_handler(int) {
    g_running.store(false);
}

struct JitterStats {
    uint64_t count = 0;
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint64_t sum = 0;
    uint64_t sum_sq = 0;
    
    void record(int64_t jitter_ns) {
        uint64_t abs_jitter = jitter_ns < 0 ? -jitter_ns : jitter_ns;
        count++;
        min = std::min(min, abs_jitter);
        max = std::max(max, abs_jitter);
        sum += abs_jitter;
        sum_sq += abs_jitter * abs_jitter;
    }
    
    double mean() const {
        return count > 0 ? static_cast<double>(sum) / count : 0;
    }
    
    double stddev() const {
        if (count < 2) return 0;
        double m = mean();
        return std::sqrt(static_cast<double>(sum_sq) / count - m * m);
    }
};

int main(int argc, char* argv[]) {
    uint64_t period_ns = 8333333;  // 120Hz default
    int duration_sec = 60;
    
    if (argc > 1) period_ns = std::stoull(argv[1]);
    if (argc > 2) duration_sec = std::stoi(argv[2]);
    
    // Lock memory
    mlockall(MCL_CURRENT | MCL_FUTURE);
    
    // Set SCHED_FIFO
    struct sched_param param;
    param.sched_priority = 80;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize timing
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup);
    
    uint64_t period_sec = period_ns / 1000000000L;
    uint64_t period_nsec = period_ns % 1000000000L;
    
    JitterStats stats;
    uint64_t deadline_misses = 0;
    
    auto start_time = std::chrono::steady_clock::now();
    auto report_time = start_time;
    
    std::cout << "Monitoring jitter for " << duration_sec << " seconds..." << std::endl;
    std::cout << "Press Ctrl+C to stop early." << std::endl;
    std::cout << std::endl;
    
    while (g_running.load()) {
        // Check duration
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        if (elapsed >= duration_sec) break;
        
        // Sleep until next period
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, nullptr);
        
        // Get actual wakeup time
        struct timespec actual;
        clock_gettime(CLOCK_MONOTONIC, &actual);
        
        // Calculate jitter
        int64_t expected_ns = next_wakeup.tv_sec * 1000000000L + next_wakeup.tv_nsec - period_ns;
        int64_t actual_ns = actual.tv_sec * 1000000000L + actual.tv_nsec;
        int64_t jitter = actual_ns - expected_ns;
        
        stats.record(jitter);
        
        // Check deadline miss
        if (jitter > static_cast<int64_t>(period_ns)) {
            deadline_misses++;
        }
        
        // Advance next_wakeup
        next_wakeup.tv_sec += period_sec;
        next_wakeup.tv_nsec += period_nsec;
        if (next_wakeup.tv_nsec >= 1000000000L) {
            next_wakeup.tv_sec += next_wakeup.tv_nsec / 1000000000L;
            next_wakeup.tv_nsec %= 1000000000L;
        }
        
        // Periodic report (every 5 seconds)
        auto report_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - report_time).count();
        if (report_elapsed >= 5) {
            std::cout << "Jitter: min=" << stats.min << "ns, max=" << stats.max 
                      << "ns, mean=" << static_cast<int64_t>(stats.mean()) << "ns"
                      << "ns, stddev=" << static_cast<int64_t>(stats.stddev()) << "ns"
                      << ", misses=" << deadline_misses << std::endl;
            
            stats = JitterStats();  // Reset
            deadline_misses = 0;
            report_time = now;
        }
    }
    
    std::cout << std::endl;
    std::cout << "=== Final Statistics ===" << std::endl;
    std::cout << "Samples:       " << stats.count << std::endl;
    std::cout << "Min jitter:    " << stats.min << " ns" << std::endl;
    std::cout << "Max jitter:    " << stats.max << " ns" << std::endl;
    std::cout << "Mean jitter:   " << static_cast<int64_t>(stats.mean()) << " ns" << std::endl;
    std::cout << "Std deviation: " << static_cast<int64_t>(stats.stddev()) << " ns" << std::endl;
    
    // Jitter requirement check (5% of period at 99.9th percentile)
    uint64_t jitter_budget = period_ns * 5 / 100;  // 5%
    std::cout << std::endl;
    std::cout << "Jitter budget (5% of period): " << jitter_budget << " ns" << std::endl;
    std::cout << "Requirement (max < budget):   " << (stats.max < jitter_budget ? "PASS" : "FAIL") << std::endl;
    
    munlockall();
    return 0;
}
CPP_CODE

# Compile monitoring program
echo "Compiling jitter monitor..."
g++ -O3 -o /tmp/jitter_monitor /tmp/jitter_monitor.cpp -lpthread -lrt

# Run monitoring
echo ""
/tmp/jitter_monitor "$PERIOD_NS" "$DURATION"

echo ""
echo "Monitoring complete."
