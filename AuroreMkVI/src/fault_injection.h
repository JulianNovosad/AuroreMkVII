#ifndef FAULT_INJECTION_H
#define FAULT_INJECTION_H

#include <atomic>
#include <chrono>
#include <functional>
#include <random>
#include <vector>
#include <cstdint>

namespace aurore {
namespace faultinjection {

enum class FaultType {
    FRAME_DROP,
    SENSOR_LOSS,
    CORRUPTED_DATA,
    TIMEOUT,
    QUEUE_OVERFLOW,
    MEMORY_ERROR
};

struct FaultConfig {
    FaultType type;
    double probability;
    int burst_count;
    int burst_probability;
    std::chrono::milliseconds delay;
    bool enabled;
};

class FaultInjector {
public:
    static FaultInjector& instance() {
        static FaultInjector injector;
        return injector;
    }
    
    void configure(FaultType type, double probability, int burst_count = 1, int burst_prob = 1) {
        configs_[static_cast<size_t>(type)].probability = probability;
        configs_[static_cast<size_t>(type)].burst_count = burst_count;
        configs_[static_cast<size_t>(type)].burst_probability = burst_prob;
        configs_[static_cast<size_t>(type)].burst_probability = burst_prob;
        configs_[static_cast<size_t>(type)].enabled = true;
    }
    
    void enable(FaultType type) { configs_[static_cast<size_t>(type)].enabled = true; }
    void disable(FaultType type) { configs_[static_cast<size_t>(type)].enabled = false; }
    
    bool should_inject(FaultType type) {
        if (!configs_[static_cast<size_t>(type)].enabled) return false;
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_) < configs_[static_cast<size_t>(type)].probability;
    }
    
    int get_burst_count(FaultType type) {
        if (!configs_[static_cast<size_t>(type)].enabled) return 0;
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        int bursts = 1;
        while (dist(rng_) < configs_[static_cast<size_t>(type)].burst_probability / 100.0 && 
               bursts < configs_[static_cast<size_t>(type)].burst_count) {
            bursts++;
        }
        return bursts;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& config : configs_) {
            config.enabled = false;
            config.probability = 0.0;
            config.burst_count = 1;
            config.burst_probability = 0;
        }
    }
    
    void inject_frame_drop(int& frame_id) {
        if (should_inject(FaultType::FRAME_DROP)) {
            frame_id = -1;  // Mark as dropped
        }
    }
    
    void inject_data_corruption(void* data, size_t size) {
        if (should_inject(FaultType::CORRUPTED_DATA)) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::uniform_int_distribution<size_t> dist(0, size - 1);
            std::uniform_int_distribution<int> bit_dist(0, 7);
            unsigned char* bytes = static_cast<unsigned char*>(data);
            bytes[dist(rng_)] ^= (1 << bit_dist(rng_));
        }
    }
    
private:
    FaultInjector() {
        for (auto& config : configs_) {
            config.type = FaultType::FRAME_DROP;
            config.probability = 0.0;
            config.burst_count = 1;
            config.burst_probability = 0;
            config.delay = std::chrono::milliseconds(0);
            config.enabled = false;
        }
    }
    
    std::array<FaultConfig, 6> configs_;
    std::mt19937 rng_{std::random_device{}()};
    std::mutex mutex_;
};

inline bool inject_frame_drop() {
    return FaultInjector::instance().should_inject(FaultType::FRAME_DROP);
}

inline bool inject_sensor_loss() {
    return FaultInjector::instance().should_inject(FaultType::SENSOR_LOSS);
}

inline void inject_memory_error(void* data, size_t size) {
    FaultInjector::instance().inject_data_corruption(data, size);
}

}  // namespace faultinjection
}  // namespace aurore

#endif  // FAULT_INJECTION_H
