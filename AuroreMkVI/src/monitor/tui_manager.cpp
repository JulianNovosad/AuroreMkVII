#include "tui_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip> // For std::fixed and std::setprecision

namespace aurore {
namespace monitor {

TuiManager::TuiManager(SharedMetrics& metrics) : metrics_(metrics), running_(false) {}

TuiManager::~TuiManager() {
    stop();
    // Ensure terminal is reset
    std::cout << "\033[0m" << std::endl; // Reset all attributes
    std::cout << "\033[?1049l" << std::endl; // Exit alternate screen buffer
    std::cout << "\033[?25h" << std::endl; // Show cursor
}

void TuiManager::start() {
    if (running_.load()) {
        return;
    }
    running_.store(true);
    // Enter alternate screen buffer and hide cursor
    std::cout << "\033[?1049h" << std::endl; // Enter alternate screen buffer
    std::cout << "\033[?25l" << std::endl; // Hide cursor
    display_thread_ = std::thread(&TuiManager::display_loop, this);
}

void TuiManager::stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (display_thread_.joinable()) {
        display_thread_.join();
    }
}

void TuiManager::display_loop() {
    while (running_.load()) {
        update_display();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 1 Hz update
    }
}

void TuiManager::update_display() {
    clear_screen();
    move_cursor(1, 1);

    std::lock_guard<std::mutex> lock(metrics_.mutex); // Lock to access string members

    // Header
    std::cout << "\033[1;36mAURORE MK VI - LIVE MONITORING\033[0m" << std::endl; // Bold Cyan

    // Throughput
    print_at(3, 1, "--- Data Throughput (Events/sec) ---");
    print_at(4, 1, "Camera In:  " + std::to_string(metrics_.camera_queue_in_count.load()));
    print_at(5, 1, "Camera Out: " + std::to_string(metrics_.camera_queue_out_count.load()));
    print_at(6, 1, "Inference In: " + std::to_string(metrics_.inference_queue_in_count.load()));
    print_at(7, 1, "Inference Out: " + std::to_string(metrics_.inference_queue_out_count.load()));

    // Invariance Checks
    print_at(9, 1, "--- Invariance Checks ---");
    std::string safety_status = metrics_.safety_invariant_violated.load() ? "\033[1;31mVIOLATED\033[0m" : "\033[1;32mOK\033[0m"; // Red for violated, Green for OK
    print_at(10, 1, "Safety: " + safety_status);
    print_at(11, 1, "Reason: " + metrics_.safety_violation_reason);

    // FPS Metrics
    print_at(13, 1, "--- FPS Metrics ---");
    std::stringstream ss_cam_fps, ss_inf_fps, ss_sys_fps;
    ss_cam_fps << std::fixed << std::setprecision(2) << metrics_.camera_fps.load();
    ss_inf_fps << std::fixed << std::setprecision(2) << metrics_.inference_fps.load();
    ss_sys_fps << std::fixed << std::setprecision(2) << metrics_.total_system_fps.load();
    print_at(14, 1, "Camera FPS:    " + ss_cam_fps.str());
    print_at(15, 1, "Inference FPS: " + ss_inf_fps.str());
    print_at(16, 1, "System FPS:    " + ss_sys_fps.str());

    // System Metrics
    print_at(18, 1, "--- System Metrics ---");
    std::stringstream ss_cpu_temp, ss_cpu_usage, ss_mem_usage, ss_swap_usage;
    ss_cpu_temp << std::fixed << std::setprecision(1) << metrics_.cpu_temp_c.load();
    ss_cpu_usage << std::fixed << std::setprecision(1) << metrics_.cpu_usage_percent.load();
    ss_mem_usage << std::fixed << std::setprecision(1) << metrics_.mem_usage_mb.load();
    ss_swap_usage << std::fixed << std::setprecision(1) << metrics_.swap_usage_mb.load();
    print_at(19, 1, "CPU Temp: " + ss_cpu_temp.str() + " C");
    print_at(20, 1, "CPU Usage: " + ss_cpu_usage.str() + " %");
    print_at(21, 1, "Mem Used: " + ss_mem_usage.str() + " MB");
    print_at(22, 1, "Swap Used: " + ss_swap_usage.str() + " MB");

    // Queue Overflows
    print_at(24, 1, "--- Queue Status ---");
    print_at(25, 1, "Overflows: " + std::to_string(metrics_.queue_overflow_count.load()));

    // Inference Details
    print_at(27, 1, "--- Inference Details ---");
    std::stringstream ss_inf_latency;
    ss_inf_latency << std::fixed << std::setprecision(2) << metrics_.inference_latency_ms.load();
    print_at(28, 1, "Latency: " + ss_inf_latency.str() + " ms");
    print_at(29, 1, "Delegate: " + metrics_.inference_delegate_type);

    std::cout.flush();
}

void TuiManager::clear_screen() {
    std::cout << "\033[2J"; // Clear entire screen
}

void TuiManager::move_cursor(int row, int col) {
    std::cout << "\033[" << row << ";" << col << "H"; // Move cursor to row, col
}

void TuiManager::print_at(int row, int col, const std::string& text) {
    move_cursor(row, col);
    std::cout << text;
}

} // namespace monitor
} // namespace aurore
