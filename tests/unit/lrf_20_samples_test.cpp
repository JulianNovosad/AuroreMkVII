#include "aurore/drivers/laser_rangefinder.hpp"
#include "aurore/timing.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <chrono>

using namespace aurore;

int main() {
    LaserRangefinder lrf;
    
    std::cout << "===========================================\n";
    std::cout << "M01 LRF - 20 Consecutive Distance Samples\n";
    std::cout << "===========================================\n\n";
    
    if (!lrf.init("/dev/ttyAMA0", 9600, LrfProtocol::M01)) {
        std::cerr << "FAIL: UART init failed\n";
        return 1;
    }



    if (!lrf.start_continuous()) {
        std::cerr << "FAIL: start_continuous failed\n";
        return 1;
    }
    
    std::vector<float> samples;
    std::cout << "\nCollecting 20 samples:\n";
    std::cout << std::fixed << std::setprecision(3);
    
    for (int i = 0; i < 20; ++i) {
        float range = lrf.latest_range_m();
        if (range > 0.0f) {
            samples.push_back(range);
            std::cout << "[" << std::setw(2) << (i+1) << "] " << range << " m\n";
        } else {
            std::cout << "[" << std::setw(2) << (i+1) << "] no reading\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    lrf.stop();
    
    if (samples.size() >= 10) {
        float sum = 0.0f;
        float min_val = samples[0], max_val = samples[0];
        for (float s : samples) {
            sum += s;
            if (s < min_val) min_val = s;
            if (s > max_val) max_val = s;
        }
        float mean = sum / static_cast<float>(samples.size());
        
        std::cout << "\n===========================================\n";
        std::cout << "Statistics (n=" << samples.size() << "):\n";
        std::cout << "  Mean: " << mean << " m\n";
        std::cout << "  Min:  " << min_val << " m\n";
        std::cout << "  Max:  " << max_val << " m\n";
        std::cout << "===========================================\n";
    }
    
    return 0;
}
