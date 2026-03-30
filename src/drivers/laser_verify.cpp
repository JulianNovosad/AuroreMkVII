// laser_verify — standalone diagnostic for M01 laser rangefinder.
// Usage: sudo ./laser_verify [/dev/ttyAMAxx]
// Prints 5 distance readings in continuous mode, then exits.

#include "aurore/drivers/laser_rangefinder.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    const std::string device = (argc > 1) ? argv[1] : "/dev/ttyAMA10";

    aurore::LaserRangefinder lrf;

    if (!lrf.init(device)) {
        std::cerr << "FAIL: UART init failed on " << device << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "UART " << device << " open OK\n";

    if (!lrf.start_continuous()) {
        std::cerr << "FAIL: start_continuous() failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Continuous mode started\n";

    // Wait for first reading then print 5 samples (M01 needs ~3-5s warm-up)
    for (int attempt = 0; attempt < 60 && lrf.latest_range_m() == 0.0f; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (int i = 0; i < 5; ++i) {
        const float m = lrf.latest_range_m();
        if (m == 0.0f) {
            std::cout << "[" << i << "] no valid reading yet\n";
        } else {
            std::cout << "[" << i << "] " << m << " m\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    lrf.stop();
    return EXIT_SUCCESS;
}
