#include <iostream>
#include <cassert>
#include <thread>
#include <pthread.h>
#include "thread_affinity.h"

using namespace aurore::threading;

void test_thread_config() {
    std::cout << "=== Testing Thread Configuration ===" << std::endl;

    ThreadConfig capture_config = ThreadAffinityManager::get_config(ThreadRole::CAPTURE);
    assert(capture_config.role == ThreadRole::CAPTURE);
    assert(capture_config.rt_priority == 90);
    assert(capture_config.scheduler_policy == ThreadAffinityManager::AURORE_SCHED_FIFO);
    assert(capture_config.affinity_mask == ThreadAffinityManager::CORE_0);
    std::cout << "  CAPTURE config: OK" << std::endl;

    ThreadConfig tpu_config = ThreadAffinityManager::get_config(ThreadRole::TPU_WORKER);
    assert(tpu_config.role == ThreadRole::TPU_WORKER);
    assert(tpu_config.rt_priority == 80);
    assert(tpu_config.scheduler_policy == ThreadAffinityManager::AURORE_SCHED_FIFO);
    assert(tpu_config.affinity_mask == ThreadAffinityManager::CORE_1);
    std::cout << "  TPU_WORKER config: OK" << std::endl;

    ThreadConfig gpu_config = ThreadAffinityManager::get_config(ThreadRole::GPU_WORKER);
    assert(gpu_config.role == ThreadRole::GPU_WORKER);
    assert(gpu_config.rt_priority == 70);
    assert(gpu_config.scheduler_policy == ThreadAffinityManager::AURORE_SCHED_RR);
    assert(gpu_config.affinity_mask == ThreadAffinityManager::LITTLE_CORES);
    std::cout << "  GPU_WORKER config: OK" << std::endl;

    ThreadConfig logic_config = ThreadAffinityManager::get_config(ThreadRole::LOGIC_OVERLAY);
    assert(logic_config.role == ThreadRole::LOGIC_OVERLAY);
    assert(logic_config.rt_priority == 60);
    assert(logic_config.scheduler_policy == ThreadAffinityManager::AURORE_SCHED_RR);
    assert(logic_config.affinity_mask == (ThreadAffinityManager::CORE_0 | ThreadAffinityManager::CORE_1 | ThreadAffinityManager::CORE_2 | ThreadAffinityManager::CORE_3));
    std::cout << "  LOGIC_OVERLAY config: OK" << std::endl;

    ThreadConfig monitor_config = ThreadAffinityManager::get_config(ThreadRole::MONITOR);
    assert(monitor_config.role == ThreadRole::MONITOR);
    assert(monitor_config.rt_priority == 0);
    assert(monitor_config.scheduler_policy == ThreadAffinityManager::AURORE_SCHED_OTHER);
    assert(monitor_config.affinity_mask == ThreadAffinityManager::ALL_CORES);
    std::cout << "  MONITOR config: OK" << std::endl;

    std::cout << "  All thread config tests passed!" << std::endl;
}

void test_affinity_string() {
    std::cout << "=== Testing Affinity String ===" << std::endl;

    std::string capture_affinity = ThreadAffinityManager::get_affinity_string(ThreadRole::CAPTURE);
    assert(capture_affinity.find("Core 0") != std::string::npos);
    std::cout << "  CAPTURE affinity: " << capture_affinity << std::endl;

    std::string tpu_affinity = ThreadAffinityManager::get_affinity_string(ThreadRole::TPU_WORKER);
    assert(tpu_affinity.find("Core 1") != std::string::npos);
    std::cout << "  TPU_WORKER affinity: " << tpu_affinity << std::endl;

    std::string gpu_affinity = ThreadAffinityManager::get_affinity_string(ThreadRole::GPU_WORKER);
    assert(gpu_affinity.find("Core 2") != std::string::npos);
    assert(gpu_affinity.find("Core 3") != std::string::npos);
    std::cout << "  GPU_WORKER affinity: " << gpu_affinity << std::endl;

    std::cout << "  All affinity string tests passed!" << std::endl;
}

void test_scheduler_string() {
    std::cout << "=== Testing Scheduler String ===" << std::endl;

    std::string capture_scheduler = ThreadAffinityManager::get_scheduler_string(ThreadRole::CAPTURE);
    assert(capture_scheduler.find("SCHED_FIFO") != std::string::npos);
    assert(capture_scheduler.find("priority 90") != std::string::npos);
    std::cout << "  CAPTURE scheduler: " << capture_scheduler << std::endl;

    std::string gpu_scheduler = ThreadAffinityManager::get_scheduler_string(ThreadRole::GPU_WORKER);
    assert(gpu_scheduler.find("SCHED_RR") != std::string::npos);
    assert(gpu_scheduler.find("priority 70") != std::string::npos);
    std::cout << "  GPU_WORKER scheduler: " << gpu_scheduler << std::endl;

    std::string monitor_scheduler = ThreadAffinityManager::get_scheduler_string(ThreadRole::MONITOR);
    assert(monitor_scheduler.find("SCHED_OTHER") != std::string::npos);
    std::cout << "  MONITOR scheduler: " << monitor_scheduler << std::endl;

    std::cout << "  All scheduler string tests passed!" << std::endl;
}

void test_apply_capture_affinity() {
    std::cout << "=== Testing Apply Capture Affinity ===" << std::endl;

    ThreadAffinityManager::apply_capture_thread_affinity();

    bool verified = ThreadAffinityManager::verify_thread_affinity(ThreadRole::CAPTURE);
    (void)verified;
    assert(verified);
    std::cout << "  Capture thread affinity verified: OK" << std::endl;
}

void test_apply_monitor_affinity() {
    std::cout << "=== Testing Apply Monitor Affinity ===" << std::endl;

    ThreadAffinityManager::apply_monitor_affinity();

    bool verified = ThreadAffinityManager::verify_thread_affinity(ThreadRole::MONITOR);
    (void)verified;
    assert(verified);
    std::cout << "  Monitor thread affinity verified: OK" << std::endl;
}

void test_thread_role_enum() {
    std::cout << "=== Testing Thread Role Enum Values ===" << std::endl;

    assert(static_cast<int>(ThreadRole::CAPTURE) == 0);
    assert(static_cast<int>(ThreadRole::TPU_WORKER) == 1);
    assert(static_cast<int>(ThreadRole::GPU_WORKER) == 2);
    assert(static_cast<int>(ThreadRole::LOGIC_OVERLAY) == 3);
    assert(static_cast<int>(ThreadRole::MONITOR) == 4);
    std::cout << "  All enum values are correct!" << std::endl;
}

void test_affinity_mask_constants() {
    std::cout << "=== Testing Affinity Mask Constants ===" << std::endl;

    assert(ThreadAffinityManager::CORE_0 == (1ULL << 0));
    assert(ThreadAffinityManager::CORE_1 == (1ULL << 1));
    assert(ThreadAffinityManager::CORE_2 == (1ULL << 2));
    assert(ThreadAffinityManager::CORE_3 == (1ULL << 3));
    assert(ThreadAffinityManager::BIG_CORES == (ThreadAffinityManager::CORE_0 | ThreadAffinityManager::CORE_1));
    assert(ThreadAffinityManager::LITTLE_CORES == (ThreadAffinityManager::CORE_2 | ThreadAffinityManager::CORE_3));
    assert(ThreadAffinityManager::ALL_CORES == (ThreadAffinityManager::BIG_CORES | ThreadAffinityManager::LITTLE_CORES));
    std::cout << "  All affinity mask constants are correct!" << std::endl;
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  Thread Affinity Test Suite" << std::endl;
    std::cout << "======================================" << std::endl;

    try {
        test_thread_role_enum();
        test_affinity_mask_constants();
        test_thread_config();
        test_affinity_string();
        test_scheduler_string();
        test_apply_capture_affinity();
        test_apply_monitor_affinity();

        std::cout << std::endl;
        std::cout << "======================================" << std::endl;
        std::cout << "  ALL TESTS PASSED!" << std::endl;
        std::cout << "======================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "TEST FAILED with unknown exception" << std::endl;
        return 1;
    }
}
