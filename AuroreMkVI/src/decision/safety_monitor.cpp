#include "safety_monitor.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>

namespace aurore {

SafetyMonitor::SafetyMonitor(Telemetry& telemetry, int kill_switch_gpio)
    : telemetry_(telemetry), actuation_allowed_(true), kill_switch_gpio_(kill_switch_gpio),
      current_temp_(0), current_usage_(0), kill_switch_engaged_(false),
      last_actuation_ms_(0),
      last_total_user(0), last_total_user_low(0), last_total_sys(0), last_total_idle(0) {
    
    init_gpio();
    update(); // Initial reading
}

SafetyMonitor::~SafetyMonitor() {
    std::cout << "DEBUG: SafetyMonitor destructor enter" << std::endl;
    cleanup_gpio();
    std::cout << "DEBUG: SafetyMonitor destructor exit" << std::endl;
}

void SafetyMonitor::update() {
    current_temp_ = read_temp();
    current_usage_ = read_cpu_usage();
    kill_switch_engaged_ = read_kill_switch();

    bool thermal_ok = (current_temp_ < 80.0f);
    bool cpu_ok = (current_usage_ < 95.0f);
    bool kill_switch_ok = !kill_switch_engaged_;

    bool previously_allowed = actuation_allowed_;
    actuation_allowed_ = thermal_ok && cpu_ok && kill_switch_ok;

    if (previously_allowed && !actuation_allowed_) {
        std::string reason = "";
        if (!thermal_ok) reason += "thermal_violation(" + std::to_string(current_temp_) + ") ";
        if (!cpu_ok) reason += "cpu_overload(" + std::to_string(current_usage_) + ") ";
        if (!kill_switch_ok) reason += "kill_switch_engaged ";
        
        telemetry_.log_safety_invariant_violation(reason);
    }
}

float SafetyMonitor::read_temp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open()) return 0;
    float temp;
    file >> temp;
    return temp / 1000.0f;
}

float SafetyMonitor::read_cpu_usage() {
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    std::istringstream ss(line);
    std::string cpu;
    unsigned long long user, nice, system, idle;
    ss >> cpu >> user >> nice >> system >> idle;

    unsigned long long total = (user - last_total_user) + (nice - last_total_user_low) +
                               (system - last_total_sys);
    unsigned long long total_plus_idle = total + (idle - last_total_idle);
    
    float percent = (total_plus_idle == 0) ? 0 : (float)total / total_plus_idle * 100.0f;

    last_total_user = user;
    last_total_user_low = nice;
    last_total_sys = system;
    last_total_idle = idle;

    return percent;
}

bool SafetyMonitor::init_gpio() {
    std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(kill_switch_gpio_);
    if (access(gpio_path.c_str(), F_OK) == -1) {
        std::ofstream export_file("/sys/class/gpio/export");
        export_file << kill_switch_gpio_;
    }
    
    std::ofstream dir_file(gpio_path + "/direction");
    dir_file << "in";
    return true;
}

void SafetyMonitor::cleanup_gpio() {
    std::ofstream unexport_file("/sys/class/gpio/unexport");
    unexport_file << kill_switch_gpio_;
}

bool SafetyMonitor::read_kill_switch() {
    std::string val_path = "/sys/class/gpio/gpio" + std::to_string(kill_switch_gpio_) + "/value";
    std::ifstream file(val_path);
    if (!file.is_open()) return true; // Fail-closed
    int val;
    file >> val;
    return (val == 0); // Assuming active-low kill switch
}

bool SafetyMonitor::validate_actuation(int pulse_us) {
    if (!actuation_allowed_) return false;

    // Boundary check
    if (pulse_us < MIN_PULSE_US || pulse_us > MAX_PULSE_US) {
        telemetry_.log_safety_invariant_violation("pulse_boundary_violation:" + std::to_string(pulse_us));
        return false;
    }

    // Temporal gate
    uint64_t now = telemetry_.get_monotonic_ms();
    if (now - last_actuation_ms_ < static_cast<uint64_t>(MIN_ACTUATION_INTERVAL_MS)) {
        // Just silent drop or log? Gemini doctrine: "Software-enforced cooldowns mandatory"
        return false; 
    }

    last_actuation_ms_ = now;
    return true;
}

} // namespace aurore