#ifndef AURORE_HARDWARE_KILLSWITCH_H
#define AURORE_HARDWARE_KILLSWITCH_H

#include <atomic>
#include <functional>
#include <thread>
#include <chrono>
#include <string>
#include <fstream>
#include <iostream>

namespace Aurore {

class HardwareKillSwitch {
public:
    static constexpr const char* GPIO_PATH = "/sys/class/gpio/gpio26/value";
    static constexpr int GPIO_PIN = 26;
    static constexpr bool ACTIVE_LOW = true;

private:
    std::atomic<bool> kill_switch_verified_;
    std::atomic<bool> running_;
    std::thread monitor_thread_;
    std::function<void(bool)> state_callback_;
    std::function<void(const std::string&)> audit_callback_;

public:
    HardwareKillSwitch() : kill_switch_verified_(false), running_(true) {
        if (!gpio_exists()) {
            std::cerr << "Kill switch GPIO " << GPIO_PIN << " not configured" << std::endl;
            return;
        }
        monitor_thread_ = std::thread(&HardwareKillSwitch::monitor_loop, this);
    }

    ~HardwareKillSwitch() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    HardwareKillSwitch(const HardwareKillSwitch&) = delete;
    HardwareKillSwitch& operator=(const HardwareKillSwitch&) = delete;

    bool verify_kill_switch() {
        bool state = read_gpio_state();
        kill_switch_verified_ = state;
        if (audit_callback_) {
            audit_callback_("Kill switch verified: " + std::string(state ? "ENGAGED" : "DISENGAGED"));
        }
        return state;
    }

    bool is_verified() const {
        return kill_switch_verified_.load();
    }

    void invalidate() {
        kill_switch_verified_ = false;
        if (audit_callback_) {
            audit_callback_("Kill switch invalidated");
        }
    }

    void set_state_callback(std::function<void(bool)> callback) {
        state_callback_ = callback;
    }

    void set_audit_callback(std::function<void(const std::string&)> callback) {
        audit_callback_ = callback;
    }

    bool read_gpio_state() const {
        std::ifstream gpio_file(GPIO_PATH);
        if (!gpio_file.is_open()) {
            return false;
        }
        int value;
        gpio_file >> value;
        gpio_file.close();
        return ACTIVE_LOW ? (value == 0) : (value == 1);
    }

    static bool gpio_exists() {
        std::ifstream gpio_file(GPIO_PATH);
        return gpio_file.good();
    }

    static bool export_gpio(int pin) {
        std::ofstream export_file("/sys/class/gpio/export");
        if (!export_file.is_open()) {
            return false;
        }
        export_file << pin;
        export_file.close();
        return true;
    }

    static bool unexport_gpio(int pin) {
        std::ofstream unexport_file("/sys/class/gpio/unexport");
        if (!unexport_file.is_open()) {
            return false;
        }
        unexport_file << pin;
        unexport_file.close();
        return true;
    }

    static bool set_gpio_direction(int pin, bool output) {
        std::string path = "/sys/class/gpio/gpio" + std::to_string(pin) + "/direction";
        std::ofstream dir_file(path);
        if (!dir_file.is_open()) {
            return false;
        }
        dir_file << (output ? "out" : "in");
        dir_file.close();
        return true;
    }

private:
    void monitor_loop() {
        while (running_) {
            bool current_state = read_gpio_state();
            
            if (current_state != kill_switch_verified_.load()) {
                kill_switch_verified_ = current_state;
                
                if (state_callback_) {
                    state_callback_(current_state);
                }
                
                if (audit_callback_) {
                    audit_callback_("Kill switch state changed: " + std::string(current_state ? "ENGAGED" : "DISENGAGED"));
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

class KillSwitchIntegrator {
private:
    HardwareKillSwitch& kill_switch_;
    std::function<bool()> software_override_check_;

public:
    explicit KillSwitchIntegrator(HardwareKillSwitch& ks) : kill_switch_(ks) {
        kill_switch_.set_state_callback([this](bool engaged) {
            on_kill_switch_change(engaged);
        });
    }

    void set_software_override_check(std::function<bool()> callback) {
        software_override_check_ = callback;
    }

    bool verify_all_systems() {
        bool hw_ok = kill_switch_.verify_kill_switch();
        bool sw_ok = software_override_check_ ? software_override_check_() : true;
        
        if (!hw_ok) {
            if (kill_switch_.audit_callback_) {
                kill_switch_.audit_callback_("System verification FAILED: Hardware kill switch disengaged");
            }
        }
        
        return hw_ok && sw_ok;
    }

    bool is_system_safe() const {
        return kill_switch_.is_verified();
    }

private:
    void on_kill_switch_change(bool engaged) {
        if (!engaged) {
            if (kill_switch_.audit_callback_) {
                kill_switch_.audit_callback_("CRITICAL: Kill switch disengaged - initiating safe shutdown");
            }
        }
    }
};

} // namespace Aurore

#endif // AURORE_HARDWARE_KILLSWITCH_H
