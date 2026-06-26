#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace aurore {

/**
 * @brief LRF communication protocol selection
 */
enum class LrfProtocol : uint8_t {
    M01 = 0,         ///< M01 9-byte continuous mode (0xAA sync, passive)
    MODBUS_RTU = 1,  ///< Modbus RTU poll/response (active polling)
};

// Laser rangefinder driver over UART (9600 8N1).
// Supports two protocols:
//   - M01: 50m continuous-mode (0xAA sync, 9-byte frames)
//   - Modbus RTU: Generic poll/response (7-byte response, distance big-endian)
// Spawns a background thread for continuous reading / periodic polling.
// All public methods are thread-safe.
class LaserRangefinder {
   public:
    static constexpr float kMaxRangeM = 50.0f;
    static constexpr float kMinRangeM = 0.05f;

    // M01 continuous-mode response frame: 13 bytes total
    // Per official M01 protocol (github.com/Andres-ros/laser-m01-esp32):
    // [0]=0xAA sync, [1-2]=header, [3]=func(0x20/0x21/0x22), [4]=0x00, [5]=0x04 length
    // [6-9]=Distance in BCD (4 bytes = 8 digits, e.g. 0x00001234 = 12.34m)
    // [10-11]=additional data, [12]=checksum (sum of bytes 1-11)
    static constexpr int kM01FrameLen = 13;
    static constexpr int kM01DistOffset = 6;    // BCD distance at bytes 6-9
    static constexpr int kM01MinFrameLen = 9;   // Minimum frame we accept
    static constexpr int kM01MaxFrameLen = 13;  // Full frame length

    // Modbus RTU constants
    static constexpr uint8_t kModbusAddr = 0x01;
    static constexpr uint8_t kModbusFunc = 0x03;  // Read Holding Registers
    static constexpr int kModbusResponseLen = 7;
    static constexpr int kModbusPollIntervalMs = 100;  // 10 Hz polling

    LaserRangefinder() = default;
    ~LaserRangefinder() { stop(); }

    // Not copyable or movable (owns a thread and fd).
    LaserRangefinder(const LaserRangefinder&) = delete;
    LaserRangefinder& operator=(const LaserRangefinder&) = delete;

    // Open UART device and configure raw mode at the given baud rate.
    // Returns true on success. On RPi 5 the GPIO14/15 UART is /dev/ttyAMA0.
    bool init(const std::string& uart_device = "/dev/ttyAMA0", int baud = 9600,
              LrfProtocol protocol = LrfProtocol::M01);

    // True after a successful init().
    bool is_ready() const noexcept { return fd_ >= 0; }

    // Start background thread: continuous mode (M01) or periodic polling (Modbus).
    // Must be called after init(). Returns false if not ready.
    bool start_continuous();

    // Signal background thread to stop, join it, then close the fd.
    void stop();

    // Latest valid range in metres; 0.0 = no valid reading yet.
    float latest_range_m() const noexcept;

    // CLOCK_MONOTONIC_RAW timestamp (ns) of the last valid reading; 0 = none.
    uint64_t last_reading_ns() const noexcept {
        return last_ts_ns_.load(std::memory_order_acquire);
    }

    // Active protocol.
    LrfProtocol protocol() const noexcept { return protocol_; }

    // Diagnostic counters (lock-free, read from any thread)
    uint32_t frames_received() const noexcept {
        return frames_received_.load(std::memory_order_acquire);
    }
    uint32_t status_frames_received() const noexcept {
        return status_frames_.load(std::memory_order_acquire);
    }
    uint32_t crc_errors() const noexcept { return crc_errors_.load(std::memory_order_acquire); }
    uint32_t frame_errors() const noexcept { return frame_errors_.load(std::memory_order_acquire); }

    /**
     * @brief Wiring diagnostic: check if UART TX/RX lines are functional
     *
     * Sends a poll command and checks for any response within 500ms.
     * Returns a diagnostic code:
     *   0 = OK (valid response received)
     *   1 = No response (possible TX/RX swap or disconnected)
     *   2 = Garbage response (possible baud rate mismatch)
     *   3 = Wrong protocol response (unexpected frame structure)
     */
    int diagnose_wiring();

    /**
     * @brief Fast hardware probe: check if device is present and responsive
     *
     * Returns true if any response received within 200ms, false otherwise.
     * Does NOT start continuous mode - just checks for activity.
     */
    bool probe_present(int timeout_ms = 200);

   private:
    void reader_loop_m01();
    void reader_loop_modbus();

    // Modbus RTU CRC-16 (polynomial 0xA001, init 0xFFFF)
    static uint16_t modbus_crc16(const uint8_t* data, size_t len) noexcept;

    // M01 checksum: sum(bytes[1..N-1]) & 0xFF
    static uint8_t m01_checksum(const uint8_t* data, size_t len) noexcept;

    // M01 BCD to millimeters: 4 BCD bytes -> millimeters
    static uint32_t m01_bcd_to_mm(const uint8_t* bcd) noexcept;

    // Attempt to find and validate a complete M01 frame in the ring buffer.
    // Returns number of bytes consumed (0 = no complete frame found).
    size_t parse_m01_frame(const uint8_t* buf, size_t len, uint32_t& out_mm);

    int fd_{-1};
    LrfProtocol protocol_{LrfProtocol::M01};
    std::atomic<uint32_t> range_mm_{0};  // 0 = no valid reading
    std::atomic<uint64_t> last_ts_ns_{0};
    std::atomic<bool> running_{false};
    std::thread reader_thread_;

    // Diagnostic counters
    std::atomic<uint32_t> frames_received_{0};
    std::atomic<uint32_t> status_frames_{
        0};  // 0xEE status/warm-up frames (LRF connected, no target)
    std::atomic<uint32_t> crc_errors_{0};
    std::atomic<uint32_t> frame_errors_{0};
};

}  // namespace aurore
