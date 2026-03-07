#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <cassert>
#include <vector>

enum class DelegateType {
    EDGE_TPU,
    XNNPACK,
    CPU
};

struct DelegateStatus {
    DelegateType type;
    bool available;
    float performance_score;
    bool in_use;
};

class TpuDelegateChain {
public:
    TpuDelegateChain()
        : current_delegate_(DelegateType::EDGE_TPU),
          delegate_count_(0),
          fallback_count_(0) {}

    bool initialize() {
        available_delegates_.clear();

        auto tpu_status = check_edge_tpu();
        available_delegates_.push_back(tpu_status);

        auto xnnpack_status = check_xnnpack();
        available_delegates_.push_back(xnnpack_status);

        auto cpu_status = check_cpu();
        available_delegates_.push_back(cpu_status);

        return has_available_delegate();
    }

    DelegateType get_current_delegate() const {
        return current_delegate_.load();
    }

    bool use_delegate(DelegateType type) {
        for (auto& status : available_delegates_) {
            if (status.type == type && status.available) {
                status.in_use = true;
                current_delegate_.store(type);
                delegate_count_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    bool fallback_to_next() {
        for (auto it = available_delegates_.begin(); it != available_delegates_.end(); ++it) {
            if (it->type == current_delegate_.load()) {
                auto next_it = std::next(it);
                while (next_it != available_delegates_.end()) {
                    if (next_it->available) {
                        next_it->in_use = true;
                        current_delegate_.store(next_it->type);
                        fallback_count_.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    }
                    ++next_it;
                }
                return false;
            }
        }
        return false;
    }

    bool has_available_delegate() const {
        for (const auto& status : available_delegates_) {
            if (status.available) return true;
        }
        return false;
    }

    int get_delegate_count() const { return delegate_count_.load(); }
    int get_fallback_count() const { return fallback_count_.load(); }
    int get_available_count() const {
        int count = 0;
        for (const auto& status : available_delegates_) {
            if (status.available) count++;
        }
        return count;
    }

    std::string get_delegate_name(DelegateType type) const {
        switch (type) {
            case DelegateType::EDGE_TPU: return "EdgeTPU";
            case DelegateType::XNNPACK: return "XNNPACK";
            case DelegateType::CPU: return "CPU";
            default: return "Unknown";
        }
    }

private:
    DelegateStatus check_edge_tpu() {
        return {DelegateType::EDGE_TPU, true, 100.0f, false};
    }

    DelegateStatus check_xnnpack() {
        return {DelegateType::XNNPACK, true, 60.0f, false};
    }

    DelegateStatus check_cpu() {
        return {DelegateType::CPU, true, 20.0f, false};
    }

    std::vector<DelegateStatus> available_delegates_;
    std::atomic<DelegateType> current_delegate_;
    std::atomic<int> delegate_count_;
    std::atomic<int> fallback_count_;
};

namespace {

bool test_initialization() {
    std::cout << "Test: Delegate chain initialization... " << std::flush;

    TpuDelegateChain chain;
    if (!chain.initialize()) {
        std::cerr << "FAILED: Initialization failed" << std::endl;
        return false;
    }

    if (chain.get_available_count() != 3) {
        std::cerr << "FAILED: Expected 3 available delegates, got " << chain.get_available_count() << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_delegate_selection() {
    std::cout << "Test: Delegate selection... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    if (!chain.use_delegate(DelegateType::EDGE_TPU)) {
        std::cerr << "FAILED: Could not select EdgeTPU" << std::endl;
        return false;
    }

    if (chain.get_current_delegate() != DelegateType::EDGE_TPU) {
        std::cerr << "FAILED: Current delegate is not EdgeTPU" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_fallback_chain() {
    std::cout << "Test: Fallback chain... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    chain.use_delegate(DelegateType::EDGE_TPU);
    chain.fallback_to_next();

    if (chain.get_current_delegate() != DelegateType::XNNPACK) {
        std::cerr << "FAILED: Expected fallback to XNNPACK" << std::endl;
        return false;
    }

    chain.fallback_to_next();

    if (chain.get_current_delegate() != DelegateType::CPU) {
        std::cerr << "FAILED: Expected fallback to CPU" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_fallback_exhaustion() {
    std::cout << "Test: Fallback exhaustion... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    chain.use_delegate(DelegateType::CPU);

    if (chain.fallback_to_next()) {
        std::cerr << "FAILED: Should not fallback from CPU" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_metrics_tracking() {
    std::cout << "Test: Metrics tracking... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    chain.use_delegate(DelegateType::EDGE_TPU);
    chain.use_delegate(DelegateType::XNNPACK);
    chain.fallback_to_next();

    if (chain.get_delegate_count() != 2) {
        std::cerr << "FAILED: Expected delegate count 2, got " << chain.get_delegate_count() << std::endl;
        return false;
    }

    if (chain.get_fallback_count() != 1) {
        std::cerr << "FAILED: Expected fallback count 1, got " << chain.get_fallback_count() << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_priority_order() {
    std::cout << "Test: Priority order (TPU > XNNPACK > CPU)... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    chain.fallback_to_next();
    chain.fallback_to_next();

    if (chain.get_current_delegate() != DelegateType::CPU) {
        std::cerr << "FAILED: Final delegate should be CPU" << std::endl;
        return false;
    }

    chain.fallback_to_next();

    if (chain.get_current_delegate() != DelegateType::CPU) {
        std::cerr << "FAILED: Should remain on CPU when no more fallbacks" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

bool test_delegate_name() {
    std::cout << "Test: Delegate name resolution... " << std::flush;

    TpuDelegateChain chain;
    chain.initialize();

    if (chain.get_delegate_name(DelegateType::EDGE_TPU) != "EdgeTPU") {
        std::cerr << "FAILED: EdgeTPU name mismatch" << std::endl;
        return false;
    }

    if (chain.get_delegate_name(DelegateType::XNNPACK) != "XNNPACK") {
        std::cerr << "FAILED: XNNPACK name mismatch" << std::endl;
        return false;
    }

    if (chain.get_delegate_name(DelegateType::CPU) != "CPU") {
        std::cerr << "FAILED: CPU name mismatch" << std::endl;
        return false;
    }

    std::cout << "PASSED" << std::endl;
    return true;
}

} // namespace

int main() {
    std::cout << "=== TpuDelegateChain Tests ===" << std::endl;

    int passed = 0;
    int failed = 0;

    if (test_initialization()) passed++; else failed++;
    if (test_delegate_selection()) passed++; else failed++;
    if (test_fallback_chain()) passed++; else failed++;
    if (test_fallback_exhaustion()) passed++; else failed++;
    if (test_metrics_tracking()) passed++; else failed++;
    if (test_priority_order()) passed++; else failed++;
    if (test_delegate_name()) passed++; else failed++;

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}
