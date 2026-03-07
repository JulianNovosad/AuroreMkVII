#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "shared_metrics.hpp"

namespace aurore {
namespace monitor {

class TuiManager {
public:
    TuiManager(SharedMetrics& metrics);
    ~TuiManager();

    // Start the TUI update loop in a separate thread
    void start();
    // Stop the TUI update loop
    void stop();
    // Clear screen and redraw all metrics
    void update_display();

private:
    void display_loop();
    void clear_screen();
    void move_cursor(int row, int col);
    void print_at(int row, int col, const std::string& text);

    SharedMetrics& metrics_;
    std::atomic<bool> running_;
    std::thread display_thread_;
};

} // namespace monitor
} // namespace aurore
