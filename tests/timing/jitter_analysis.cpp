/**
 * @file jitter_analysis.cpp
 * @brief Thread wakeup jitter measurement for AM7-L2-TIM-003 compliance.
 *
 * Simulates the 120Hz vision_pipeline loop using clock_nanosleep(TIMER_ABSTIME)
 * and measures actual wakeup-interval deviation. Pass criterion:
 *   99.9th-percentile jitter ≤ 5% of 8333µs nominal period (= 417µs).
 *
 * Spec: AM7-L2-TIM-003
 */

#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include "aurore/timing.hpp"

namespace {

static constexpr uint64_t kPeriodNs       = 8333333ULL;   // 120 Hz nominal
static constexpr uint64_t kJitterLimitNs  = 416666ULL;    // 5% of 8333µs = 417µs
static constexpr size_t   kWarmupSamples  = 10;
static constexpr size_t   kDefaultSamples = 10000;        // enough for 99.9th-pctile

struct JitterStats {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t mean_ns;
    uint64_t p999_ns;  // 99.9th percentile
    bool     pass;
};

JitterStats analyse(std::vector<int64_t>& deviations) {
    // Work on absolute deviations (unsigned)
    std::vector<uint64_t> abs_dev;
    abs_dev.reserve(deviations.size());
    for (auto d : deviations) abs_dev.push_back(static_cast<uint64_t>(std::abs(d)));

    if (abs_dev.empty()) return JitterStats{};

    std::sort(abs_dev.begin(), abs_dev.end());

    JitterStats s{};
    s.min_ns = abs_dev.front();
    s.max_ns = abs_dev.back();

    uint64_t sum = std::accumulate(abs_dev.begin(), abs_dev.end(), 0ULL);
    s.mean_ns = sum / abs_dev.size();

    size_t idx999 = (abs_dev.size() * 999) / 1000;
    if (idx999 >= abs_dev.size()) idx999 = abs_dev.size() - 1;
    s.p999_ns = abs_dev[idx999];

    s.pass = (s.p999_ns <= kJitterLimitNs);
    return s;
}

}  // anonymous namespace

// Called from wcet_measurement.cpp main
void run_jitter_analysis(size_t num_samples) {
    if (num_samples == 0) num_samples = kDefaultSamples;

    std::cout << "\n=== Jitter Analysis (AM7-L2-TIM-003) ===" << std::endl;
    std::cout << "Period:       " << kPeriodNs / 1000 << " µs (120 Hz)" << std::endl;
    std::cout << "Limit:        " << kJitterLimitNs / 1000 << " µs (5% of period at P99.9)" << std::endl;
    std::cout << "Samples:      " << num_samples << std::endl;

    std::vector<int64_t> deviations;
    deviations.reserve(num_samples);

    struct timespec next{};
    clock_gettime(CLOCK_MONOTONIC, &next);

    // Advance to first target tick
    next.tv_nsec += static_cast<long>(kPeriodNs);
    if (next.tv_nsec >= 1000000000L) {
        next.tv_sec  += 1;
        next.tv_nsec -= 1000000000L;
    }

    // Warm up — discard first few samples (cache effects)
    for (size_t i = 0; i < kWarmupSamples; ++i) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        next.tv_nsec += static_cast<long>(kPeriodNs);
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec  += 1;
            next.tv_nsec -= 1000000000L;
        }
    }

    uint64_t prev = aurore::get_timestamp();

    for (size_t i = 0; i < num_samples; ++i) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

        uint64_t now = aurore::get_timestamp();
        int64_t interval = static_cast<int64_t>(now - prev);
        deviations.push_back(interval - static_cast<int64_t>(kPeriodNs));
        prev = now;

        // Advance absolute target
        next.tv_nsec += static_cast<long>(kPeriodNs);
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec  += 1;
            next.tv_nsec -= 1000000000L;
        }
    }

    JitterStats s = analyse(deviations);

    std::cout << "\nResults:" << std::endl;
    std::cout << "  Min jitter:  " << s.min_ns       / 1000 << " µs" << std::endl;
    std::cout << "  Max jitter:  " << s.max_ns       / 1000 << " µs" << std::endl;
    std::cout << "  Mean jitter: " << s.mean_ns      / 1000 << " µs" << std::endl;
    std::cout << "  P99.9:       " << s.p999_ns      / 1000 << " µs" << std::endl;
    std::cout << "  Limit:       " << kJitterLimitNs / 1000 << " µs" << std::endl;

    std::cout << "\nAM7-L2-TIM-003 (jitter ≤ 5% at P99.9): "
              << (s.pass ? "PASS" : "FAIL") << std::endl;

    if (!s.pass) {
        std::cerr << "JITTER ANALYSIS FAILED: P99.9 jitter " << s.p999_ns / 1000
                  << " µs exceeds limit " << kJitterLimitNs / 1000 << " µs\n";
        // Not a hard abort — caller decides
    }
}
