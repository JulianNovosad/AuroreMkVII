/**
 * @file interlock_controller.hpp
 * @brief GPIO-based safety interlock controller
 *
 * Hardware safety interlock using Raspberry Pi GPIO pins.
 * Prevents actuation when interlock is open (safety posture per AM7-L3-MODE-007).
 *
 * Interlock states:
 * - OPEN: Safety circuit broken, actuation inhibited
 * - CLOSED: Safety circuit complete, actuation enabled
 * - FAULT: Interlock hardware fault detected
 *
 * @copyright AuroreMkVII Project - Educational/Personal Use Only
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusion_hat.hpp"

namespace aurore {

/**
 * @brief Interlock state enumeration
 */
enum class InterlockState : uint8_t {
    OPEN,    ///< Safety circuit open - actuation inhibited
    CLOSED,  ///< Safety circuit closed - actuation enabled
    FAULT,   ///< Hardware fault detected
    UNKNOWN  ///< Initial state before first read
};

/**
 * @brief Convert interlock state to string
 */
inline const char* interlock_state_to_string(InterlockState state) noexcept {
    switch (state) {
        case InterlockState::OPEN:
            return "OPEN";
        case InterlockState::CLOSED:
            return "CLOSED";
        case InterlockState::FAULT:
            return "FAULT";
        case InterlockState::UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
    }
}

/**
 * @brief Interlock controller configuration
 */
struct InterlockConfig {
    /// GPIO pin for interlock input (BCM numbering)
    int input_pin = 17;

    /// PWM channel on Fusion HAT+ for inhibit (ICD-003 specifies channel 2)
    int inhibit_channel = 2;

    /// GPIO pin for status LED (BCM numbering)
    int status_led_pin = 22;

    /// Debounce time in milliseconds
    uint32_t debounce_ms = 50;

    /// Polling interval in milliseconds
    uint32_t poll_interval_ms = 10;

    /// Active low input (true = grounded when closed)
    bool active_low = true;

    /// Enable hardware watchdog
    bool enable_watchdog = true;

    /// Watchdog timeout in milliseconds
    uint32_t watchdog_timeout_ms = 1000;

    /**
     * @brief Validate configuration
     */
    bool validate() const noexcept {
        // GPIO pins must be valid BCM numbers (0-27 on Pi 5)
        if (input_pin < 0 || input_pin > 27) return false;
        if (status_led_pin < 0 || status_led_pin > 27) return false;

        // Pins must be unique
        if (input_pin == status_led_pin) return false;

        // PWM channel must be valid (0-11)
        if (inhibit_channel < 0 || inhibit_channel > 11) return false;

        return true;
    }
};

/**
 * @brief Interlock status for monitoring
 */
struct InterlockStatus {
    /// Current interlock state
    InterlockState state{InterlockState::UNKNOWN};

    /// Timestamp of last state change (nanoseconds since boot)
    uint64_t last_change_ns{0};

    /// Number of state transitions
    uint64_t transition_count{0};

    /// Number of fault events
    uint64_t fault_count{0};

    /// Watchdog feed counter
    uint64_t watchdog_feeds{0};

    /// Timestamp of last watchdog feed
    uint64_t last_watchdog_feed_ns{0};

    /// True if actuation is currently inhibited
    bool actuation_inhibited{true};
};

/**
 * @brief Hybrid GPIO/I2C safety interlock controller
 *
 * Monitors hardware interlock circuit (GPIO) and controls Fusion HAT+ inhibit (I2C).
 * Thread-safe for concurrent access from safety monitor and main loop.
 */
class InterlockController {
   public:
    /**
     * @brief Construct interlock controller
     *
     * @param hat Fusion HAT+ instance for I2C inhibit control (nullptr for testing)
     * @param config Interlock configuration
     */
    InterlockController(FusionHat* hat = nullptr, const InterlockConfig& config = InterlockConfig());

    /**
     * @brief Destructor
     */
    ~InterlockController();

    // Non-copyable
    InterlockController(const InterlockController&) = delete;
    InterlockController& operator=(const InterlockController&) = delete;

    /**
     * @brief Initialize GPIO hardware
     *
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Start interlock monitoring thread
     */
    void start();

    /**
     * @brief Stop interlock monitoring thread
     */
    void stop();

    /**
     * @brief Check if controller is running
     */
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Get current interlock state
     */
    InterlockState get_state() const noexcept { return state_.load(std::memory_order_acquire); }

    /**
     * @brief Get current status snapshot
     */
    InterlockStatus get_status() const noexcept;

    /**
     * @brief Check if actuation is allowed
     *
     * @return true if interlock is CLOSED and no faults
     */
    bool is_actuation_allowed() const noexcept {
        return state_.load(std::memory_order_acquire) == InterlockState::CLOSED;
    }

    /**
     * @brief Feed the hardware watchdog
     *
     * Must be called at least every watchdog_timeout_ms to prevent fault.
     * Call from safety monitor thread at 1kHz.
     */
    void watchdog_feed() noexcept;

    /**
     * @brief Manually set inhibit state (for testing)
     *
     * @param inhibit true to inhibit actuation, false to allow
     */
    void set_inhibit(bool inhibit) noexcept;

    /**
     * @brief Force interlock state (for testing/emergency)
     *
     * @param state State to force
     */
    void force_state(InterlockState state) noexcept;

    /**
     * @brief Get GPIO input state (raw, for debugging)
     *
     * @return int GPIO input value (0 or 1)
     */
    int get_input_raw() const noexcept { return input_value_.load(std::memory_order_acquire); }

    /**
     * @brief Get GPIO output state (raw, for debugging)
     *
     * @return int GPIO output value (0 or 1)
     */
    int get_output_raw() const noexcept { return output_value_.load(std::memory_order_acquire); }

   private:
    /**
     * @brief Monitoring thread function
     */
    void monitor_thread_func() noexcept;

    /**
     * @brief Read GPIO input with debouncing
     *
     * @return int Debounced input value (0 or 1)
     */
    int read_input_debounced() noexcept;

    /**
     * @brief Update inhibit output based on state
     */
    void update_inhibit_output() noexcept;

    /**
     * @brief Update status LED based on state
     */
    void update_status_led() noexcept;

    FusionHat* hat_;
    InterlockConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<InterlockState> state_{InterlockState::UNKNOWN};
    std::atomic<int> input_value_{0};
    std::atomic<int> output_value_{0};

    // GPIO state (internal implementation)
    struct GpioState {
        volatile uint32_t* gpio_map;
        size_t gpio_map_size;
        int mem_fd;

        GpioState() : gpio_map(nullptr), gpio_map_size(0), mem_fd(-1) {}

        bool init();
        void cleanup();
        void set_pin_mode(int pin, int mode);
        int read_pin(int pin);
        void write_pin(int pin, int value);
    };

    std::unique_ptr<GpioState> impl_;

    // Status tracking
    mutable std::atomic<uint64_t> last_change_ns_{0};
    mutable std::atomic<uint64_t> transition_count_{0};
    mutable std::atomic<uint64_t> fault_count_{0};
    mutable std::atomic<uint64_t> watchdog_feeds_{0};
    mutable std::atomic<uint64_t> last_watchdog_feed_ns_{0};

    // Debounce state
    int last_stable_input_{0};
    uint64_t debounce_start_ns_{0};
};

}  // namespace aurore
