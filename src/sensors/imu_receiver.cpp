/**
 * @file imu_receiver.cpp
 * @brief UDP receiver for SensaGram IMU data
 *
 * Spec: AM7-L2-IF-009, AM7-L2-IMU-001, ICD-008
 * Source: Android phone (Samsung A54) running SensaGram app
 * Protocol: UDP port 7070, JSON format
 * Sensors: accelerometer, gyroscope, device_orientation, orientation
 */

#include "aurore/imu_receiver.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>

#include "aurore/timing.hpp"

namespace aurore {

// Simple JSON parser for SensaGram format
// We parse manually to avoid nlohmann/json dependency
namespace {

// Find value for a key in simple JSON object
std::string find_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Find colon after key
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    // Check if value is string (starts with quote)
    if (pos < json.length() && json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    // Check if value is array (starts with bracket)
    if (pos < json.length() && json[pos] == '[') {
        size_t end = json.find(']', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos + 1);
    }

    // Numeric value - find end (comma, brace, or end of string)
    size_t end = pos;
    while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        end++;
    }

    // Trim whitespace
    while (end > pos && (json[end - 1] == ' ' || json[end - 1] == '\t')) end--;

    return json.substr(pos, end - pos);
}

// Parse JSON array of floats
std::vector<float> parse_float_array(const std::string& array_str) {
    std::vector<float> values;
    if (array_str.empty() || array_str[0] != '[') return values;

    // Remove brackets
    std::string content = array_str.substr(1, array_str.length() - 2);

    // Split by comma
    std::stringstream ss(content);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        size_t start = item.find_first_not_of(" \t");
        size_t end = item.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            try {
                float val = std::stof(item.substr(start, end - start + 1));
                values.push_back(val);
            } catch (...) {
                // Parse error - skip this value
            }
        }
    }

    return values;
}

}  // anonymous namespace

ImuReceiver::ImuReceiver(const ImuReceiverConfig& config) : config_(config), latest_data_{} {
    // Initialize latency history
    latency_history_.fill(0.f);
}

ImuReceiver::~ImuReceiver() {
    stop();
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
    }
}

bool ImuReceiver::init() {
    // Create UDP socket
    udp_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
    if (udp_fd_ < 0) {
        std::cerr << "ImuReceiver: socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    // Set socket options for reuse
    int opt = 1;
    if (::setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "ImuReceiver: setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << "\n";
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }

    // Bind to address and port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.udp_port);

    if (config_.bind_address == "127.0.0.1" || config_.bind_address == "0.0.0.0") {
        addr.sin_addr.s_addr = inet_addr(config_.bind_address.c_str());
    } else {
        std::cerr << "ImuReceiver: Invalid bind address: " << config_.bind_address << "\n";
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }

    if (::bind(udp_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "ImuReceiver: bind() failed: " << strerror(errno) << "\n";
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }

    std::cout << "ImuReceiver: Listening on " << config_.bind_address << ":" << config_.udp_port
              << "\n";

    return true;
}

void ImuReceiver::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    running_.store(true, std::memory_order_release);
    receiver_thread_ = std::thread(&ImuReceiver::receiver_loop, this);
}

void ImuReceiver::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;  // Not running
    }

    running_.store(false, std::memory_order_release);

    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }

    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
}

bool ImuReceiver::is_data_fresh() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (!latest_data_.latest_timestamp_ns) {
        return false;  // No data yet
    }

    uint64_t now = get_timestamp(ClockId::MonotonicRaw);
    uint64_t age = now - latest_data_.latest_timestamp_ns;

    return age < config_.max_sample_age_ns;
}

ImuData ImuReceiver::get_latest_data() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return latest_data_;
}

std::optional<ImuSample> ImuReceiver::get_latest_sample(ImuSensorType type) const {
    std::lock_guard<std::mutex> lock(samples_mutex_);

    // Search backwards for most recent sample of this type
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (parse_sensor_type(it->sensor_type) == type) {
            return *it;
        }
    }

    return std::nullopt;
}

void ImuReceiver::receiver_loop() {
    constexpr size_t kBufferSize = 2048;
    std::vector<char> buffer(kBufferSize);

    std::cout << "ImuReceiver: Receiver thread started\n";

    while (running_.load(std::memory_order_acquire)) {
        // Receive UDP packet (non-blocking with timeout)
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(udp_fd_, &read_fds);

        struct timeval timeout{1, 0};  // 1 second timeout
        int ret = ::select(udp_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;  // Interrupted, retry
            std::cerr << "ImuReceiver: select() error: " << strerror(errno) << "\n";
            break;
        }

        if (ret == 0) {
            // Timeout - check if we should continue
            continue;
        }

        // Receive data
        ssize_t n = ::recvfrom(udp_fd_, buffer.data(), kBufferSize, 0,
                               reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (n < 0) {
            if (errno == EAGAIN) continue;       // Would block (non-blocking socket)
            if (errno == EWOULDBLOCK) continue;  // Same as EAGAIN on some systems
            std::cerr << "ImuReceiver: recvfrom() error: " << strerror(errno) << "\n";
            continue;
        }

        if (n == 0) continue;  // Empty packet

        // Record receipt timestamp immediately
        uint64_t receipt_ts = get_timestamp(ClockId::MonotonicRaw);

        // Parse JSON message (cast n to size_t, safe since n > 0)
        std::string json(buffer.data(), static_cast<size_t>(n));
        ImuSample sample;
        sample.receipt_timestamp_ns = receipt_ts;

        parse_json_message(json, sample);

        if (!sample.valid) {
            packets_dropped_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Update statistics
        packets_received_.fetch_add(1, std::memory_order_relaxed);

        // Calculate latency (receipt - sensor timestamp)
        if (sample.sensor_timestamp_ns > 0) {
            float latency_ms = static_cast<float>(receipt_ts - sample.sensor_timestamp_ns) / 1e6f;

            // Update moving average
            latency_history_[latency_idx_ % kLatencyWindow] = latency_ms;
            latency_idx_++;

            float sum = 0.f;
            size_t count = std::min(latency_idx_, kLatencyWindow);
            for (size_t i = 0; i < count; i++) {
                sum += latency_history_[i];
            }
            avg_latency_ms_.store(sum / static_cast<float>(count), std::memory_order_relaxed);
        }

        // Add to sample buffer (circular buffer)
        {
            std::lock_guard<std::mutex> lock(samples_mutex_);
            samples_.push_back(sample);

            // Trim buffer if too large
            while (samples_.size() > config_.max_queue_size) {
                samples_.erase(samples_.begin());
            }
        }

        // Update aggregated data
        update_aggregated_data(sample);
    }

    std::cout << "ImuReceiver: Receiver thread stopped\n";
}

void ImuReceiver::parse_json_message(const std::string& json, ImuSample& sample) {
    // Parse SensaGram JSON format:
    // {"type": "android.sensor.accelerometer", "timestamp": 3925657519043709, "values": [0.32,
    // -0.98, 10.05]}

    // Extract type
    std::string type_str = find_json_value(json, "type");
    if (type_str.empty()) {
        return;
    }
    sample.sensor_type = type_str;

    // Extract timestamp
    std::string ts_str = find_json_value(json, "timestamp");
    if (!ts_str.empty()) {
        try {
            sample.sensor_timestamp_ns = std::stoull(ts_str);
        } catch (...) {
            // Parse error
        }
    }

    // Extract values array
    std::string values_str = find_json_value(json, "values");
    if (!values_str.empty()) {
        std::vector<float> values = parse_float_array(values_str);

        if (!values.empty()) {
            for (size_t i = 0; i < std::min(values.size(), sample.values.size()); i++) {
                sample.values[i] = values[i];
            }
            sample.valid = true;
        }
    }
}

ImuSensorType ImuReceiver::parse_sensor_type(const std::string& type_str) const {
    if (type_str == "android.sensor.accelerometer") {
        return ImuSensorType::kAccelerometer;
    } else if (type_str == "android.sensor.gyroscope") {
        return ImuSensorType::kGyroscope;
    } else if (type_str == "android.sensor.device_orientation") {
        return ImuSensorType::kDeviceOrientation;
    } else if (type_str == "android.sensor.orientation") {
        return ImuSensorType::kOrientation;
    }
    return ImuSensorType::kUnknown;
}

void ImuReceiver::update_aggregated_data(const ImuSample& sample) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    latest_data_.latest_timestamp_ns = sample.receipt_timestamp_ns;
    latest_data_.packets_received = packets_received_.load(std::memory_order_relaxed);
    latest_data_.packets_dropped = packets_dropped_.load(std::memory_order_relaxed);
    latest_data_.latency_ms = avg_latency_ms_.load(std::memory_order_relaxed);

    ImuSensorType type = parse_sensor_type(sample.sensor_type);

    switch (type) {
        case ImuSensorType::kAccelerometer:
            latest_data_.accel_x = sample.values[0];
            latest_data_.accel_y = sample.values[1];
            latest_data_.accel_z = sample.values[2];
            latest_data_.accel_valid = sample.valid;
            break;

        case ImuSensorType::kGyroscope:
            latest_data_.gyro_x = sample.values[0];
            latest_data_.gyro_y = sample.values[1];
            latest_data_.gyro_z = sample.values[2];
            latest_data_.gyro_valid = sample.valid;
            break;

        case ImuSensorType::kDeviceOrientation:
            latest_data_.azimuth_deg = sample.values[0];
            latest_data_.pitch_deg = sample.values[1];
            latest_data_.roll_deg = sample.values[2];
            latest_data_.orientation_valid = sample.valid;
            break;

        case ImuSensorType::kOrientation:
            latest_data_.quat_w = sample.values[0];
            latest_data_.quat_x = sample.values[1];
            latest_data_.quat_y = sample.values[2];
            latest_data_.quat_z = sample.values[3];
            latest_data_.quaternion_valid = sample.valid;
            break;

        default:
            break;
    }
}

}  // namespace aurore
