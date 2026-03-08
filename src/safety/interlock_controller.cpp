/**
 * @file interlock_controller.cpp
 * @brief GPIO-based safety interlock controller implementation
 *
 * Implementation using /dev/gpiomem for memory-mapped GPIO access.
 */

#include "aurore/interlock_controller.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include "aurore/timing.hpp"

namespace aurore {

// GPIO register offsets (BCM2835/2711/2712)
static constexpr uint32_t GPFSEL0 = 0x00;  // GPIO Function Select 0
static constexpr uint32_t GPFSEL1 = 0x04;  // GPIO Function Select 1
static constexpr uint32_t GPSET0  = 0x1C;  // GPIO Pin Output Set
static constexpr uint32_t GPCLR0  = 0x28;  // GPIO Pin Output Clear
static constexpr uint32_t GPLEV0  = 0x34;  // GPIO Pin Level

// GpioState implementation
bool InterlockController::GpioState::init() {
    // Open /dev/gpiomem (preferred - no root required)
    mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        // Fall back to /dev/mem (requires root)
        mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mem_fd < 0) {
            std::cerr << "Failed to open GPIO memory device" << std::endl;
            return false;
        }
        // GPIO base address for Pi 5 (BCM2712)
        off_t gpio_base = 0xFE200000;
        gpio_map_size = 4096;
        
        gpio_map = static_cast<volatile uint32_t*>(
            mmap(nullptr, gpio_map_size, PROT_READ | PROT_WRITE, 
                 MAP_SHARED, mem_fd, gpio_base)
        );
    } else {
        gpio_map_size = 4096;
        gpio_map = static_cast<volatile uint32_t*>(
            mmap(nullptr, gpio_map_size, PROT_READ | PROT_WRITE, 
                 MAP_SHARED, mem_fd, 0)
        );
    }
    
    if (gpio_map == MAP_FAILED) {
        std::cerr << "Failed to map GPIO memory: " << strerror(errno) << std::endl;
        close(mem_fd);
        return false;
    }
    
    return true;
}

void InterlockController::GpioState::cleanup() {
    if (gpio_map && gpio_map != MAP_FAILED) {
        munmap(const_cast<uint32_t*>(gpio_map), gpio_map_size);
        gpio_map = nullptr;
    }
    if (mem_fd >= 0) {
        close(mem_fd);
        mem_fd = -1;
    }
}

void InterlockController::GpioState::set_pin_mode(int pin, int mode) {
    // mode: 0=input, 1=output
    uint32_t reg = GPFSEL0 + (pin / 10) * 4;
    uint32_t shift = (pin % 10) * 3;
    uint32_t mask = 0b111 << shift;
    
    gpio_map[reg / 4] = (gpio_map[reg / 4] & ~mask) | (mode << shift);
    __sync_synchronize();  // Memory barrier
}

int InterlockController::GpioState::read_pin(int pin) {
    uint32_t reg = GPLEV0;
    return (gpio_map[reg / 4] >> pin) & 1;
}

void InterlockController::GpioState::write_pin(int pin, int value) {
    uint32_t reg = value ? GPSET0 : GPCLR0;
    gpio_map[reg / 4] = 1 << pin;
    __sync_synchronize();  // Memory barrier
}

}  // namespace aurore

namespace aurore {

InterlockController::InterlockController(const InterlockConfig& config)
    : config_(config)
    , impl_(std::make_unique<GpioState>())
    , last_stable_input_(0)
    , debounce_start_ns_(0) {
}

InterlockController::~InterlockController() {
    stop();
    if (impl_) {
        impl_->cleanup();
    }
}

bool InterlockController::init() {
    if (!config_.validate()) {
        std::cerr << "Invalid interlock configuration" << std::endl;
        return false;
    }
    
    if (!impl_ || !impl_->init()) {
        std::cerr << "GPIO initialization failed" << std::endl;
        return false;
    }
    
    // Configure pins
    impl_->set_pin_mode(config_.input_pin, 0);      // Input
    impl_->set_pin_mode(config_.inhibit_pin, 1);    // Output
    impl_->set_pin_mode(config_.status_led_pin, 1); // Output
    
    // Set initial output state (inhibit active)
    impl_->write_pin(config_.inhibit_pin, config_.active_low ? 0 : 1);
    impl_->write_pin(config_.status_led_pin, 0);
    
    std::cout << "Interlock controller initialized:"
              << " input=GPIO" << config_.input_pin
              << ", inhibit=GPIO" << config_.inhibit_pin
              << ", led=GPIO" << config_.status_led_pin
              << std::endl;
    
    return true;
}

void InterlockController::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    
    running_.store(true, std::memory_order_release);
    
    // Start monitoring thread
    std::thread(&InterlockController::monitor_thread_func, this).detach();
    
    std::cout << "Interlock monitoring started" << std::endl;
}

void InterlockController::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    running_.store(false, std::memory_order_release);
    
    // Set inhibit output (safe state)
    impl_->write_pin(config_.inhibit_pin, config_.active_low ? 0 : 1);
    impl_->write_pin(config_.status_led_pin, 0);
    
    std::cout << "Interlock monitoring stopped" << std::endl;
}

InterlockStatus InterlockController::get_status() const noexcept {
    InterlockStatus status;
    status.state = state_.load(std::memory_order_acquire);
    status.last_change_ns = last_change_ns_.load(std::memory_order_acquire);
    status.transition_count = transition_count_.load(std::memory_order_acquire);
    status.fault_count = fault_count_.load(std::memory_order_acquire);
    status.watchdog_feeds = watchdog_feeds_.load(std::memory_order_acquire);
    status.last_watchdog_feed_ns = last_watchdog_feed_ns_.load(std::memory_order_acquire);
    status.actuation_inhibited = !is_actuation_allowed();
    return status;
}

void InterlockController::watchdog_feed() noexcept {
    if (!config_.enable_watchdog) return;
    
    const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);
    last_watchdog_feed_ns_.store(now, std::memory_order_release);
    watchdog_feeds_.fetch_add(1, std::memory_order_relaxed);
    
    // Check for watchdog timeout
    const TimestampNs last_feed = last_watchdog_feed_ns_.load(std::memory_order_acquire);
    const int64_t elapsed = timestamp_diff_ns(now, last_feed);
    
    if (elapsed > static_cast<int64_t>(config_.watchdog_timeout_ms * 1000000UL)) {
        // Watchdog timeout - trigger fault
        fault_count_.fetch_add(1, std::memory_order_relaxed);
        force_state(InterlockState::FAULT);
    }
}

void InterlockController::set_inhibit(bool inhibit) noexcept {
    impl_->write_pin(config_.inhibit_pin, inhibit ? (config_.active_low ? 0 : 1) 
                                                   : (config_.active_low ? 1 : 0));
    output_value_.store(impl_->read_pin(config_.inhibit_pin), std::memory_order_release);
}

void InterlockController::force_state(InterlockState state) noexcept {
    InterlockState expected = state_.load(std::memory_order_acquire);
    while (!state_.compare_exchange_weak(expected, state,
            std::memory_order_acq_rel, std::memory_order_acquire)) {}
    
    if (expected != state) {
        last_change_ns_.store(get_timestamp(ClockId::MonotonicRaw), std::memory_order_release);
        transition_count_.fetch_add(1, std::memory_order_relaxed);
        update_inhibit_output();
        update_status_led();
    }
}

int InterlockController::read_input_debounced() noexcept {
    const int raw_input = impl_->read_pin(config_.input_pin);
    input_value_.store(raw_input, std::memory_order_release);
    
    const int expected_input = config_.active_low ? 0 : 1;
    const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);
    
    if (raw_input == expected_input) {
        // Input matches expected state
        if (last_stable_input_ != expected_input) {
            // First time seeing this state
            debounce_start_ns_ = now;
            last_stable_input_ = expected_input;
        } else {
            // Check if debounce period has elapsed
            const int64_t debounce_elapsed = timestamp_diff_ns(now, debounce_start_ns_);
            if (debounce_elapsed >= static_cast<int64_t>(config_.debounce_ms * 1000000UL)) {
                // Debounce complete
                return expected_input;
            }
        }
    } else {
        // Input doesn't match expected state
        last_stable_input_ = raw_input;
    }
    
    // Return previous stable state during debounce
    return last_stable_input_;
}

void InterlockController::update_inhibit_output() noexcept {
    if (!impl_ || !impl_->gpio_map) return;
    const InterlockState state = state_.load(std::memory_order_acquire);
    const bool inhibit = (state != InterlockState::CLOSED);
    set_inhibit(inhibit);
}

void InterlockController::update_status_led() noexcept {
    if (!impl_ || !impl_->gpio_map) return;
    const InterlockState state = state_.load(std::memory_order_acquire);
    
    // LED patterns:
    // - OFF: OPEN (safe)
    // - ON: CLOSED (armed)
    // - Blink: FAULT
    switch (state) {
        case InterlockState::OPEN:
            impl_->write_pin(config_.status_led_pin, 0);
            break;
        case InterlockState::CLOSED:
            impl_->write_pin(config_.status_led_pin, 1);
            break;
        case InterlockState::FAULT:
            // Blink at 5Hz
            {
                const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);
                const uint32_t ms = static_cast<uint32_t>((now % 200000000UL) / 1000000UL);
                impl_->write_pin(config_.status_led_pin, ms < 100 ? 1 : 0);
            }
            break;
        case InterlockState::UNKNOWN:
            impl_->write_pin(config_.status_led_pin, 0);
            break;
    }
}

void InterlockController::monitor_thread_func() noexcept {
    const uint32_t poll_interval_us = config_.poll_interval_ms * 1000;
    
    while (running_.load(std::memory_order_acquire)) {
        const TimestampNs start = get_timestamp(ClockId::MonotonicRaw);
        
        // Read debounced input
        const int input = read_input_debounced();
        const int expected = config_.active_low ? 0 : 1;
        
        // Update state based on input
        InterlockState new_state;
        if (input == expected) {
            new_state = InterlockState::CLOSED;
        } else {
            new_state = InterlockState::OPEN;
        }
        
        // Check for watchdog timeout
        if (config_.enable_watchdog) {
            const TimestampNs now = get_timestamp(ClockId::MonotonicRaw);
            const TimestampNs last_feed = last_watchdog_feed_ns_.load(std::memory_order_acquire);
            const int64_t elapsed = timestamp_diff_ns(now, last_feed);
            
            if (elapsed > static_cast<int64_t>(config_.watchdog_timeout_ms * 1000000UL)) {
                new_state = InterlockState::FAULT;
                fault_count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        // Update state if changed
        InterlockState old_state = state_.load(std::memory_order_acquire);
        while (new_state != old_state &&
               !state_.compare_exchange_weak(old_state, new_state,
                   std::memory_order_acq_rel, std::memory_order_acquire)) {}
        
        if (new_state != old_state) {
            last_change_ns_.store(get_timestamp(ClockId::MonotonicRaw), std::memory_order_release);
            transition_count_.fetch_add(1, std::memory_order_relaxed);
            update_inhibit_output();
        }
        
        update_status_led();
        
        // Sleep until next poll interval
        const TimestampNs end = get_timestamp(ClockId::MonotonicRaw);
        const int64_t elapsed = timestamp_diff_ns(end, start);
        const int64_t sleep_us = static_cast<int64_t>(poll_interval_us) - elapsed / 1000;
        
        if (sleep_us > 0) {
            struct timespec ts{};
            ts.tv_sec = sleep_us / 1000000;
            ts.tv_nsec = (sleep_us % 1000000) * 1000;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
        }
    }
}

}  // namespace aurore
