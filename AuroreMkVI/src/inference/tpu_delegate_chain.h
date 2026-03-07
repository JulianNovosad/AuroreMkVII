#ifndef TPU_DELEGATE_CHAIN_H
#define TPU_DELEGATE_CHAIN_H

#include <vector>
#include <atomic>
#include <string>
#include <memory>
#include <functional>

#include "tensorflow/lite/c/c_api.h"
#include "edgetpu_c.h"

enum class TpuDelegateType {
    EDGE_TPU,
    XNNPACK,
    CPU
};

struct TpuDelegateStatus {
    TpuDelegateType type;
    bool available;
    float performance_score;
    bool in_use;
    TfLiteDelegate* delegate;
    std::string name;

    TpuDelegateStatus() : type(TpuDelegateType::CPU), available(false),
                          performance_score(0.0f), in_use(false), delegate(nullptr) {}
};

class TpuDelegateChain {
public:
    TpuDelegateChain();
    ~TpuDelegateChain();

    bool initialize();
    void shutdown();

    bool use_delegate(TpuDelegateType type);
    bool fallback_to_next();
    bool can_fallback() const;

    TpuDelegateType get_current_delegate() const { return current_delegate_.load(); }
    TfLiteDelegate* get_current_tf_delegate() const;

    int get_delegate_count() const { return delegate_count_.load(); }
    int get_fallback_count() const { return fallback_count_.load(); }
    int get_available_count() const;

    std::string get_current_delegate_name() const;
    static std::string delegate_type_to_name(TpuDelegateType type);

    bool has_edge_tpu() const { return edge_tpu_available_; }
    bool has_xnnpack() const { return xnnpack_available_; }

    void set_fallback_enabled(bool enabled) { fallback_enabled_.store(enabled, std::memory_order_release); }
    bool is_fallback_enabled() const { return fallback_enabled_.load(std::memory_order_acquire); }

private:
    TpuDelegateStatus create_edge_tpu_delegate();
    TpuDelegateStatus create_xnnpack_delegate();
    TpuDelegateStatus create_cpu_delegate();

    bool check_edge_tpu_availability();
    bool check_xnnpack_availability();

    std::vector<TpuDelegateStatus> delegates_;
    std::atomic<TpuDelegateType> current_delegate_;
    std::atomic<int> delegate_count_;
    std::atomic<int> fallback_count_;
    std::atomic<bool> fallback_enabled_;
    std::atomic<bool> edge_tpu_available_;
    std::atomic<bool> xnnpack_available_;
    bool initialized_;
};

#endif // TPU_DELEGATE_CHAIN_H
