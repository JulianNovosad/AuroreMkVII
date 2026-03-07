#ifndef THREAD_AFFINITY_H
#define THREAD_AFFINITY_H

#include <thread>
#include <string>
#include <cstdint>

namespace aurore {
namespace threading {

enum class ThreadRole {
    CAPTURE,
    TPU_WORKER,
    GPU_WORKER,
    LOGIC_OVERLAY,
    MAIN_LOOP,
    MONITOR
};

struct ThreadConfig {
    ThreadRole role;
    int rt_priority;
    int scheduler_policy;
    uint64_t affinity_mask;
    std::string name;
};

class ThreadAffinityManager {
public:
    static constexpr int AURORE_SCHED_FIFO = 1;
    static constexpr int AURORE_SCHED_RR = 2;
    static constexpr int AURORE_SCHED_OTHER = 0;

    static constexpr uint64_t CORE_0 = (1ULL << 0);
    static constexpr uint64_t CORE_1 = (1ULL << 1);
    static constexpr uint64_t CORE_2 = (1ULL << 2);
    static constexpr uint64_t CORE_3 = (1ULL << 3);
    static constexpr uint64_t BIG_CORES = CORE_0 | CORE_1;
    static constexpr uint64_t LITTLE_CORES = CORE_2 | CORE_3;
    static constexpr uint64_t ALL_CORES = BIG_CORES | LITTLE_CORES;

    static ThreadConfig get_config(ThreadRole role) {
        switch (role) {
            case ThreadRole::CAPTURE:
                return {
                    ThreadRole::CAPTURE,
                    90,
                    AURORE_SCHED_FIFO,
                    CORE_0,
                    "CaptureThread"
                };
            case ThreadRole::TPU_WORKER:
                return {
                    ThreadRole::TPU_WORKER,
                    80,
                    AURORE_SCHED_FIFO,
                    CORE_2,
                    "TPUWorkerThread"
                };
            case ThreadRole::GPU_WORKER:
                return {
                    ThreadRole::GPU_WORKER,
                    75,
                    AURORE_SCHED_RR,
                    CORE_1,
                    "GPUWorkerThread"
                };
            case ThreadRole::LOGIC_OVERLAY:
                return {
                    ThreadRole::LOGIC_OVERLAY,
                    70,
                    AURORE_SCHED_RR,
                    CORE_1,
                    "LogicOverlayThread"
                };
            case ThreadRole::MAIN_LOOP:
                return {
                    ThreadRole::MAIN_LOOP,
                    85,
                    AURORE_SCHED_FIFO,
                    CORE_3,
                    "MainLoopThread"
                };
            case ThreadRole::MONITOR:
                return {
                    ThreadRole::MONITOR,
                    0,
                    AURORE_SCHED_OTHER,
                    ALL_CORES,
                    "MonitorThread"
                };
            default:
                return {
                    ThreadRole::MONITOR,
                    0,
                    AURORE_SCHED_OTHER,
                    ALL_CORES,
                    "UnknownThread"
                };
        }
    }

    static bool set_thread_affinity(ThreadRole role) {
        ThreadConfig config = get_config(role);
        return set_thread_affinity(config);
    }

    static bool set_thread_affinity(const ThreadConfig& config) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        for (int i = 0; i < 64; ++i) {
            if (config.affinity_mask & (1ULL << i)) {
                CPU_SET(i, &cpuset);
            }
        }

        pthread_t current_thread = pthread_self();
        int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (result != 0) {
            return false;
        }

        if (config.scheduler_policy != AURORE_SCHED_OTHER) {
            return set_thread_scheduler(config);
        }

        return true;
    }

    static bool set_thread_scheduler(const ThreadConfig& config) {
        sched_param sched_param_data;
        sched_param_data.sched_priority = config.rt_priority;

        int result = pthread_setschedparam(
            pthread_self(),
            config.scheduler_policy,
            &sched_param_data
        );

        return result == 0;
    }

    static void apply_capture_thread_affinity() {
        set_thread_affinity(ThreadRole::CAPTURE);
    }

    static void apply_tpu_worker_affinity() {
        set_thread_affinity(ThreadRole::TPU_WORKER);
    }

    static void apply_gpu_worker_affinity() {
        set_thread_affinity(ThreadRole::GPU_WORKER);
    }

    static void apply_logic_overlay_affinity() {
        set_thread_affinity(ThreadRole::LOGIC_OVERLAY);
    }

    static void apply_main_loop_affinity() {
        set_thread_affinity(ThreadRole::MAIN_LOOP);
    }

    static void apply_monitor_affinity() {
        set_thread_affinity(ThreadRole::MONITOR);
    }

    static bool verify_thread_affinity(ThreadRole expected_role) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        pthread_t current_thread = pthread_self();
        int result = pthread_getaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (result != 0) {
            return false;
        }

        ThreadConfig expected_config = get_config(expected_role);
        cpu_set_t expected_set;
        CPU_ZERO(&expected_set);

        for (int i = 0; i < 64; ++i) {
            if (expected_config.affinity_mask & (1ULL << i)) {
                CPU_SET(i, &expected_set);
            }
        }

        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpuset) != CPU_ISSET(i, &expected_set)) {
                return false;
            }
        }

        return true;
    }

    static std::string get_affinity_string(ThreadRole role) {
        ThreadConfig config = get_config(role);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        for (int i = 0; i < 64; ++i) {
            if (config.affinity_mask & (1ULL << i)) {
                CPU_SET(i, &cpuset);
            }
        }

        std::string result;
        bool first = true;
        for (int i = 0; i < 64; ++i) {
            if (CPU_ISSET(i, &cpuset)) {
                if (!first) {
                    result += ", ";
                }
                result += "Core " + std::to_string(i);
                first = false;
            }
        }
        return result;
    }

    static std::string get_scheduler_string(ThreadRole role) {
        ThreadConfig config = get_config(role);
        std::string policy_str;

        switch (config.scheduler_policy) {
            case AURORE_SCHED_FIFO:
                policy_str = "SCHED_FIFO";
                break;
            case AURORE_SCHED_RR:
                policy_str = "SCHED_RR";
                break;
            case AURORE_SCHED_OTHER:
            default:
                policy_str = "SCHED_OTHER";
                break;
        }

        return policy_str + " (priority " + std::to_string(config.rt_priority) + ")";
    }
};

}
}

#endif // THREAD_AFFINITY_H
