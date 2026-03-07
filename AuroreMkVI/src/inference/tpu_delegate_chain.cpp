#include "tpu_delegate_chain.h"
#include "util_logging.h"
#include <algorithm>

TpuDelegateChain::TpuDelegateChain()
    : current_delegate_(TpuDelegateType::CPU),
      delegate_count_(0),
      fallback_count_(0),
      fallback_enabled_(true),
      edge_tpu_available_(false),
      xnnpack_available_(false),
      initialized_(false) {}

TpuDelegateChain::~TpuDelegateChain() {
    shutdown();
}

bool TpuDelegateChain::initialize() {
    if (initialized_) {
        APP_LOG_WARNING("TpuDelegateChain already initialized");
        return true;
    }

    delegates_.clear();

    APP_LOG_INFO("Initializing TPU delegate chain with fallback support...");

    bool has_any_delegate = false;

    auto tpu_status = create_edge_tpu_delegate();
    if (tpu_status.available) {
        edge_tpu_available_.store(true, std::memory_order_release);
        delegates_.push_back(tpu_status);
        has_any_delegate = true;
        APP_LOG_INFO("Edge TPU delegate available");
    } else {
        APP_LOG_WARNING("Edge TPU not available, will use fallback");
    }

    auto xnnpack_status = create_xnnpack_delegate();
    if (xnnpack_status.available) {
        xnnpack_available_.store(true, std::memory_order_release);
        delegates_.push_back(xnnpack_status);
        has_any_delegate = true;
        APP_LOG_INFO("XNNPACK delegate available");
    }

    auto cpu_status = create_cpu_delegate();
    if (cpu_status.available) {
        delegates_.push_back(cpu_status);
        has_any_delegate = true;
        APP_LOG_INFO("CPU delegate available");
    }

    if (!has_any_delegate) {
        APP_LOG_ERROR("No delegates available");
        return false;
    }

    current_delegate_.store(TpuDelegateType::EDGE_TPU);
    initialized_ = true;

    APP_LOG_INFO("TPU delegate chain initialized with " + std::to_string(get_available_count()) + " available delegates");
    return true;
}

void TpuDelegateChain::shutdown() {
    if (!initialized_) return;

    for (auto& status : delegates_) {
        if (status.delegate) {
            APP_LOG_INFO("Freeing delegate: " + status.name);
        }
    }

    delegates_.clear();
    initialized_ = false;
}

TpuDelegateStatus TpuDelegateChain::create_edge_tpu_delegate() {
    TpuDelegateStatus status;
    status.type = TpuDelegateType::EDGE_TPU;
    status.name = "EdgeTPU";
    status.delegate = nullptr;

    size_t num_devices = 0;
    edgetpu_device* devices = edgetpu_list_devices(&num_devices);

    if (num_devices > 0 && devices != nullptr) {
        status.available = true;
        status.performance_score = 100.0f;
        status.delegate = edgetpu_create_delegate(devices[0].type, devices[0].path, nullptr, 0);
        edgetpu_free_devices(devices);

        if (!status.delegate) {
            APP_LOG_WARNING("Edge TPU device found but delegate creation failed");
            status.available = false;
        }
    } else {
        APP_LOG_DEBUG("No Edge TPU devices found");
    }

    return status;
}

TpuDelegateStatus TpuDelegateChain::create_xnnpack_delegate() {
    TpuDelegateStatus status;
    status.type = TpuDelegateType::XNNPACK;
    status.name = "XNNPACK";
    status.delegate = nullptr;

    status.available = true;
    status.performance_score = 60.0f;

    APP_LOG_INFO("XNNPACK delegate configured (CPU-based acceleration)");

    return status;
}

TpuDelegateStatus TpuDelegateChain::create_cpu_delegate() {
    TpuDelegateStatus status;
    status.type = TpuDelegateType::CPU;
    status.name = "CPU";
    status.delegate = nullptr;

    status.available = true;
    status.performance_score = 20.0f;

    APP_LOG_INFO("CPU delegate configured (baseline)");

    return status;
}

bool TpuDelegateChain::use_delegate(TpuDelegateType type) {
    for (auto& status : delegates_) {
        if (status.type == type && status.available) {
            status.in_use = true;
            current_delegate_.store(type);
            delegate_count_.fetch_add(1, std::memory_order_relaxed);
            APP_LOG_INFO("Switched to delegate: " + delegate_type_to_name(type));
            return true;
        }
    }
    APP_LOG_WARNING("Delegate not available: " + delegate_type_to_name(type));
    return false;
}

bool TpuDelegateChain::fallback_to_next() {
    if (!fallback_enabled_.load(std::memory_order_acquire)) {
        APP_LOG_DEBUG("Fallback disabled, not switching delegates");
        return false;
    }

    TpuDelegateType current = current_delegate_.load();
    bool found_current = false;

    for (auto it = delegates_.begin(); it != delegates_.end(); ++it) {
        if (it->type == current) {
            found_current = true;
            auto next_it = std::next(it);
            while (next_it != delegates_.end()) {
                if (next_it->available) {
                    next_it->in_use = true;
                    current_delegate_.store(next_it->type);
                    fallback_count_.fetch_add(1, std::memory_order_relaxed);
                    APP_LOG_WARNING("Fallback to: " + next_it->name +
                                   " (fallback count: " + std::to_string(fallback_count_.load()) + ")");
                    return true;
                }
                ++next_it;
            }
            break;
        }
    }

    APP_LOG_WARNING("No fallback available from: " + delegate_type_to_name(current));
    return false;
}

bool TpuDelegateChain::can_fallback() const {
    TpuDelegateType current = current_delegate_.load();
    bool found_current = false;

    for (auto it = delegates_.begin(); it != delegates_.end(); ++it) {
        if (it->type == current) {
            found_current = true;
            auto next_it = std::next(it);
            while (next_it != delegates_.end()) {
                if (next_it->available) return true;
                ++next_it;
            }
            break;
        }
    }

    return false;
}

TfLiteDelegate* TpuDelegateChain::get_current_tf_delegate() const {
    for (const auto& status : delegates_) {
        if (status.type == current_delegate_.load()) {
            return status.delegate;
        }
    }
    return nullptr;
}

int TpuDelegateChain::get_available_count() const {
    int count = 0;
    for (const auto& status : delegates_) {
        if (status.available) count++;
    }
    return count;
}

std::string TpuDelegateChain::get_current_delegate_name() const {
    return delegate_type_to_name(current_delegate_.load());
}

std::string TpuDelegateChain::delegate_type_to_name(TpuDelegateType type) {
    switch (type) {
        case TpuDelegateType::EDGE_TPU: return "EdgeTPU";
        case TpuDelegateType::XNNPACK: return "XNNPACK";
        case TpuDelegateType::CPU: return "CPU";
        default: return "Unknown";
    }
}
