#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aurore {

/**
 * @brief IMU sensor sample from SensaGram app
 *
 * Spec: AM7-L2-IF-009, ICD-008
 * Source: Android phone (Samsung A54) running SensaGram
 * Protocol: UDP port 7070, JSON format
 */
struct ImuSample {
    uint64_t sensor_timestamp_ns{0};                  ///< Android SensorEvent.timestamp
    uint64_t receipt_timestamp_ns{0};                 ///< Pi 5 CLOCK_MONOTONIC_RAW on receipt
    std::string sensor_type;                          ///< "android.sensor.accelerometer", etc.
    std::array<float, 4> values{0.f, 0.f, 0.f, 0.f};  ///< Sensor values (up to 4)
    bool valid{false};                                ///< Sample validity flag
};

/**
 * @brief IMU sensor type enumeration
 */
enum class ImuSensorType : uint8_t {
    kAccelerometer = 0,
    kGyroscope = 1,
    kDeviceOrientation = 2,
    kOrientation = 3,
    kUnknown = 255
};

/**
 * @brief IMU data aggregation (combined from multiple sensors)
 */
struct ImuData {
    // Accelerometer (m/s²)
    float accel_x{0.f};
    float accel_y{0.f};
    float accel_z{0.f};
    bool accel_valid{false};

    // Gyroscope (rad/s)
    float gyro_x{0.f};
    float gyro_y{0.f};
    float gyro_z{0.f};
    bool gyro_valid{false};

    // Device orientation (degrees)
    float azimuth_deg{0.f};  ///< 0-360
    float pitch_deg{0.f};    ///< -180 to 180
    float roll_deg{0.f};     ///< -180 to 180
    bool orientation_valid{false};

    // Quaternion orientation (w, x, y, z)
    float quat_w{1.f};
    float quat_x{0.f};
    float quat_y{0.f};
    float quat_z{0.f};
    bool quaternion_valid{false};

    // Metadata
    uint64_t latest_timestamp_ns{0};
    uint32_t packets_received{0};
    uint32_t packets_dropped{0};
    float latency_ms{0.f};  ///< Receipt - sensor timestamp
};

/**
 * @brief Configuration for IMU receiver
 *
 * Spec: AM7-L2-IF-009
 */
struct ImuReceiverConfig {
    std::string bind_address = "0.0.0.0";    ///< Listen on all interfaces (required for USB tether)
    uint16_t udp_port = 7070;                ///< UDP port for IMU data
    size_t max_queue_size = 100;             ///< Max samples to buffer
    uint64_t max_sample_age_ns = 100000000;  ///< 100ms max age
    bool enable_accelerometer = true;
    bool enable_gyroscope = true;
    bool enable_device_orientation = true;
    bool enable_orientation = true;
};

/**
 * @brief UDP receiver for SensaGram IMU data
 *
 * Receives JSON-formatted IMU samples from Android phone running SensaGram app.
 * Parses accelerometer, gyroscope, device_orientation, and orientation sensors.
 *
 * Spec: AM7-L2-IF-009, AM7-L2-IMU-001, ICD-008
 *
 * Usage:
 * @code
 * ImuReceiverConfig config;
 * config.udp_port = 7070;
 *
 * ImuReceiver receiver(config);
 * if (!receiver.init()) {
 *     // Handle error
 * }
 * receiver.start();
 *
 * // In control loop:
 * ImuData data = receiver.get_latest_data();
 * if (data.gyro_valid) {
 *     // Use angular velocity for gimbal stabilization
 * }
 * @endcode
 */
class ImuReceiver {
   public:
    explicit ImuReceiver(const ImuReceiverConfig& config = ImuReceiverConfig());
    ~ImuReceiver();

    /**
     * @brief Initialize UDP socket
     * @return true on success, false on failure
     */
    bool init();

    /**
     * @brief Start receiver thread
     */
    void start();

    /**
     * @brief Stop receiver thread
     */
    void stop();

    /**
     * @brief Check if receiver is running
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Check if IMU data is fresh (< 100ms old)
     */
    bool is_data_fresh() const;

    /**
     * @brief Get latest aggregated IMU data
     */
    ImuData get_latest_data() const;

    /**
     * @brief Get latest IMU sample for a specific sensor type
     */
    std::optional<ImuSample> get_latest_sample(ImuSensorType type) const;

    /**
     * @brief Get statistics
     */
    uint32_t get_packets_received() const {
        return packets_received_.load(std::memory_order_acquire);
    }
    uint32_t get_packets_dropped() const {
        return packets_dropped_.load(std::memory_order_acquire);
    }
    float get_average_latency_ms() const { return avg_latency_ms_.load(std::memory_order_acquire); }

   private:
    void receiver_loop();
    void parse_json_message(const std::string& json, ImuSample& sample);
    ImuSensorType parse_sensor_type(const std::string& type_str) const;
    void update_aggregated_data(const ImuSample& sample);

    ImuReceiverConfig config_;
    int udp_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread receiver_thread_;

    // Thread-safe sample storage
    mutable std::mutex samples_mutex_;
    std::vector<ImuSample> samples_;  ///< Circular buffer of recent samples

    // Aggregated IMU data (latest values from all sensors)
    mutable std::mutex data_mutex_;
    ImuData latest_data_;

    // Statistics
    std::atomic<uint32_t> packets_received_{0};
    std::atomic<uint32_t> packets_dropped_{0};
    std::atomic<float> avg_latency_ms_{0.f};

    // Simple moving average for latency
    static constexpr size_t kLatencyWindow = 100;
    std::array<float, kLatencyWindow> latency_history_;
    size_t latency_idx_{0};
};

}  // namespace aurore
