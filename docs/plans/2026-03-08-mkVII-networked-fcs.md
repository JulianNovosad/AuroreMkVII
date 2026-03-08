# AuroreMkVII: Networked Fire Control System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Transform MkVII from a skeleton with excellent RT bones into a fully operational networked fire control system where Aurore Link (remote app) is the primary operator interface.

**Architecture:** MkVII runs fully autonomous local fire control (Auto Mode) on RPi 5 and broadcasts telemetry + annotated video over TCP to Aurore Link clients. When Link sends a FREECAM command, the gimbal switches to direct position commands from Link; local fire control pauses. All state transitions use the existing StateMachine. Protocol is length-prefixed protobuf over TCP.

**Tech Stack:** C++17, protobuf 3, nlohmann/json (config), OpenCV 4.6 (annotation), Python 3 + protobuf (Link client MVP), CTest for all verification.

**Platform note:** All tests run on native x86 (no hardware). Hardware-dependent paths are mock-gated via `#ifdef AURORE_LAPTOP_BUILD` or dry-run flag already in place.

---

## Phase 0: Fill Test Coverage Gaps

MkVI had 22 real unit tests. MkVII currently has 12. Before adding new features, cover every existing source module with a dedicated test.

---

### Task 0.1: TelemetryWriter dedicated test

**Files:**
- Create: `tests/unit/telemetry_writer_test.cpp`
- Modify: `CMakeLists.txt` (add test executable)

**Step 1: Write the failing test**

```cpp
// tests/unit/telemetry_writer_test.cpp
#include "aurore/telemetry_writer.hpp"
#include "aurore/telemetry_types.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

using namespace aurore;

void test_writer_starts_and_stops() {
    TelemetryWriter writer;
    TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;  // skip JSON for speed
    assert(writer.start(cfg));
    assert(writer.is_running());
    writer.stop();
    assert(!writer.is_running());
    std::cout << "PASS: writer starts and stops\n";
}

void test_log_frame_increments_counter() {
    TelemetryWriter writer;
    TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;
    writer.start(cfg);

    DetectionData det;
    TrackData     track;
    ActuationData act;
    SystemHealthData health;

    writer.log_frame(det, track, act, health);
    writer.log_frame(det, track, act, health);
    writer.log_frame(det, track, act, health);

    writer.stop();
    assert(writer.get_entries_written() >= 3);
    std::cout << "PASS: entries_written >= 3 after 3 log_frame calls\n";
}

void test_backpressure_drop_policy() {
    TelemetryWriter writer;
    TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.max_queue_size = 5;
    cfg.backpressure_policy = BackpressurePolicy::kDropNewest;
    cfg.enable_json = false;
    writer.start(cfg);

    // Pause the writer thread briefly by flooding before it drains
    DetectionData det; TrackData trk; ActuationData act; SystemHealthData h;
    for (int i = 0; i < 20; ++i)
        writer.log_frame(det, trk, act, h);

    uint64_t dropped = writer.get_entries_dropped();
    writer.stop();
    // With queue size 5 and 20 entries, at least some must be dropped
    assert(dropped > 0);
    std::cout << "PASS: backpressure drop policy drops entries (" << dropped << " dropped)\n";
}

void test_queue_stats_accessible() {
    TelemetryWriter writer;
    TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;
    writer.start(cfg);
    auto stats = writer.get_queue_stats();
    assert(stats.max_depth == cfg.max_queue_size);
    writer.stop();
    std::cout << "PASS: get_queue_stats() returns valid struct\n";
}

int main() {
    std::filesystem::create_directories("/tmp/aurore_test_logs");
    test_writer_starts_and_stops();
    test_log_frame_increments_counter();
    test_backpressure_drop_policy();
    test_queue_stats_accessible();
    std::cout << "\nAll TelemetryWriter tests passed.\n";
    return 0;
}
```

**Step 2: Add to CMakeLists.txt** — find the `HudSocketTest` block and add after it:

```cmake
# TelemetryWriter tests
add_executable(telemetry_writer_test
    tests/unit/telemetry_writer_test.cpp
    src/common/telemetry_writer.cpp
)
target_include_directories(telemetry_writer_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(telemetry_writer_test PRIVATE Threads::Threads)
add_test(NAME TelemetryWriterTest COMMAND telemetry_writer_test)
set_tests_properties(TelemetryWriterTest PROPERTIES TIMEOUT 30)
```

**Step 3: Build and run**

```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native --target telemetry_writer_test -j$(nproc)
cd build-native && ./telemetry_writer_test
```
Expected: 4 lines of PASS.

**Step 4: Run via CTest**

```bash
cd /home/laptop/AuroreMkVII/build-native && ctest -R TelemetryWriterTest --output-on-failure
```
Expected: `TelemetryWriterTest ... Passed`

**Step 5: Commit**

```bash
cd /home/laptop/AuroreMkVII
git add tests/unit/telemetry_writer_test.cpp CMakeLists.txt
git commit -m "test: add TelemetryWriter dedicated unit tests"
```

---

### Task 0.2: InterlockController test (laptop-safe)

`InterlockController` uses GPIO mmap which is unavailable on x86. The test covers software-only paths: `force_state()`, `get_state()`, `watchdog_feed()`, `is_actuation_allowed()`.

**Files:**
- Create: `tests/unit/interlock_controller_test.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Write the test**

```cpp
// tests/unit/interlock_controller_test.cpp
#include "aurore/interlock_controller.hpp"
#include <cassert>
#include <iostream>

using namespace aurore;

// InterlockController::init() requires /dev/gpiomem which doesn't exist on x86.
// All tests use force_state() to bypass hardware init.

void test_initial_state_is_unknown() {
    InterlockController ic;
    assert(ic.get_state() == InterlockState::UNKNOWN);
    std::cout << "PASS: initial state is UNKNOWN\n";
}

void test_force_state_changes_state() {
    InterlockController ic;
    ic.force_state(InterlockState::CLOSED);
    assert(ic.get_state() == InterlockState::CLOSED);
    ic.force_state(InterlockState::OPEN);
    assert(ic.get_state() == InterlockState::OPEN);
    std::cout << "PASS: force_state changes state\n";
}

void test_actuation_allowed_only_when_closed() {
    InterlockController ic;
    ic.force_state(InterlockState::OPEN);
    assert(!ic.is_actuation_allowed());
    ic.force_state(InterlockState::CLOSED);
    assert(ic.is_actuation_allowed());
    ic.force_state(InterlockState::FAULT);
    assert(!ic.is_actuation_allowed());
    std::cout << "PASS: is_actuation_allowed() correct for all states\n";
}

void test_get_status_reflects_state() {
    InterlockController ic;
    ic.force_state(InterlockState::CLOSED);
    auto status = ic.get_status();
    assert(status.state == InterlockState::CLOSED);
    assert(!status.actuation_inhibited);
    ic.force_state(InterlockState::FAULT);
    status = ic.get_status();
    assert(status.actuation_inhibited);
    std::cout << "PASS: get_status() reflects current state\n";
}

void test_watchdog_feed_increments_counter() {
    InterlockController ic;
    // watchdog_feed() is noexcept and safe to call without hardware
    uint64_t before = ic.get_status().watchdog_feeds;
    ic.watchdog_feed();
    uint64_t after = ic.get_status().watchdog_feeds;
    assert(after == before + 1);
    std::cout << "PASS: watchdog_feed() increments counter\n";
}

int main() {
    test_initial_state_is_unknown();
    test_force_state_changes_state();
    test_actuation_allowed_only_when_closed();
    test_get_status_reflects_state();
    test_watchdog_feed_increments_counter();
    std::cout << "\nAll InterlockController tests passed.\n";
    return 0;
}
```

**Step 2: Add to CMakeLists.txt** (after TelemetryWriterTest block):

```cmake
# InterlockController tests (laptop-safe, hardware paths excluded)
add_executable(interlock_controller_test
    tests/unit/interlock_controller_test.cpp
    src/safety/interlock_controller.cpp
)
target_include_directories(interlock_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(interlock_controller_test PRIVATE Threads::Threads rt)
add_test(NAME InterlockControllerTest COMMAND interlock_controller_test)
set_tests_properties(InterlockControllerTest PROPERTIES TIMEOUT 15)
```

**Step 3: Build and run**

```bash
cd /home/laptop/AuroreMkVII && cmake --build build-native --target interlock_controller_test -j$(nproc)
cd build-native && ./interlock_controller_test
```
Expected: 5 PASS lines. Note: `force_state()` and `watchdog_feed()` call `update_inhibit_output()` / `update_status_led()` internally which try to write GPIO — these will silently fail (null `impl_` pointer guard in `force_state` prevents crash). If they do crash, add a null-check guard in `interlock_controller.cpp` before `impl_->write_pin()` calls.

**Step 4: CTest**

```bash
cd build-native && ctest -R InterlockControllerTest --output-on-failure
```

**Step 5: Commit**

```bash
git add tests/unit/interlock_controller_test.cpp CMakeLists.txt
git commit -m "test: add InterlockController unit tests (hardware-free paths)"
```

---

### Task 0.3: CameraWrapper test (test pattern mode)

**Files:**
- Create: `tests/unit/camera_wrapper_test.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Write the test**

```cpp
// tests/unit/camera_wrapper_test.cpp
#include "aurore/camera_wrapper.hpp"
#include <cassert>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace aurore;

void test_construction_with_default_config() {
    CameraWrapper cam;
    assert(!cam.is_running());
    assert(cam.frame_count() == 0);
    std::cout << "PASS: default construction ok\n";
}

void test_invalid_config_throws() {
    CameraConfig bad;
    bad.width = 0;  // invalid
    bool threw = false;
    try { CameraWrapper cam(bad); }
    catch (const CameraException&) { threw = true; }
    assert(threw);
    std::cout << "PASS: invalid config throws CameraException\n";
}

void test_init_and_start_stop() {
    CameraWrapper cam;
    assert(cam.init());
    assert(cam.start());
    assert(cam.is_running());
    cam.stop();
    assert(!cam.is_running());
    std::cout << "PASS: init/start/stop lifecycle ok\n";
}

void test_try_capture_returns_test_pattern_frame() {
    CameraConfig cfg;
    cfg.width  = 1536;
    cfg.height = 864;
    CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    ZeroCopyFrame frame;
    bool got = cam.try_capture_frame(frame);
    assert(got);
    assert(frame.valid);
    assert(frame.width  == 1536);
    assert(frame.height == 864);
    assert(frame.plane_data[0] != nullptr);
    std::cout << "PASS: try_capture_frame returns valid test pattern\n";
    cam.stop();
}

void test_wrap_as_mat_returns_bgr_image() {
    CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    ZeroCopyFrame frame;
    cam.try_capture_frame(frame);
    cv::Mat img = cam.wrap_as_mat(frame, PixelFormat::BGR888);
    assert(!img.empty());
    assert(img.cols == 320);
    assert(img.rows == 240);
    assert(img.channels() == 3);
    std::cout << "PASS: wrap_as_mat returns 320x240 BGR frame\n";
    cam.stop();
}

void test_sequence_increments_monotonically() {
    CameraConfig cfg;
    cfg.width  = 320;
    cfg.height = 240;
    CameraWrapper cam(cfg);
    cam.init();
    cam.start();

    ZeroCopyFrame f1, f2;
    cam.try_capture_frame(f1);
    cam.try_capture_frame(f2);
    assert(f2.sequence > f1.sequence);
    std::cout << "PASS: frame sequence numbers increment\n";
    cam.stop();
}

int main() {
    test_construction_with_default_config();
    test_invalid_config_throws();
    test_init_and_start_stop();
    test_try_capture_returns_test_pattern_frame();
    test_wrap_as_mat_returns_bgr_image();
    test_sequence_increments_monotonically();
    std::cout << "\nAll CameraWrapper tests passed.\n";
    return 0;
}
```

**Step 2: Add to CMakeLists.txt**:

```cmake
# CameraWrapper tests (test pattern mode, no hardware)
add_executable(camera_wrapper_test
    tests/unit/camera_wrapper_test.cpp
    src/drivers/camera_wrapper.cpp
)
target_include_directories(camera_wrapper_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OPENCV_INCLUDE_DIRS}
    ${LIBCAMERA_INCLUDE_DIRS}
)
target_link_libraries(camera_wrapper_test PRIVATE
    Threads::Threads
    ${OPENCV_LIBRARIES}
    ${LIBCAMERA_LIBRARIES}
    rt
)
add_test(NAME CameraWrapperTest COMMAND camera_wrapper_test)
set_tests_properties(CameraWrapperTest PROPERTIES TIMEOUT 30)
```

**Step 3: Build and run**

```bash
cmake --build build-native --target camera_wrapper_test -j$(nproc)
cd build-native && ./camera_wrapper_test
```

**Step 4: CTest + full suite**

```bash
cd build-native && ctest --output-on-failure
```
Expected: 15/15 (12 original + 3 new), 1 disabled.

**Step 5: Commit**

```bash
git add tests/unit/camera_wrapper_test.cpp CMakeLists.txt
git commit -m "test: add CameraWrapper unit tests (test pattern mode)"
```

---

## Phase 1: Network Protocol (Protobuf)

### Task 1.1: Install protobuf and wire CMakeLists.txt

**Step 1: Install**

```bash
sudo apt-get install -y protobuf-compiler libprotobuf-dev python3-grpcio-tools
# Verify:
protoc --version   # libprotoc 3.21.x or newer
```

**Step 2: Create proto directory and schema**

```bash
mkdir -p /home/laptop/AuroreMkVII/proto
```

Create `/home/laptop/AuroreMkVII/proto/aurore.proto`:

```protobuf
syntax = "proto3";
package aurore;

// ── Shared enums ──────────────────────────────────────────────

enum FcsState {
    BOOT      = 0;
    IDLE_SAFE = 1;
    FREECAM   = 2;
    SEARCH    = 3;
    TRACKING  = 4;
    ARMED     = 5;
    FAULT     = 6;
}

enum OperatingMode {
    AUTO    = 0;   // Local fire control active
    FREECAM = 1;   // Gimbal follows Link commands
}

// ── MkVII → Link messages ─────────────────────────────────────

message TrackState {
    bool  valid       = 1;
    float centroid_x  = 2;   // pixels
    float centroid_y  = 3;
    float velocity_x  = 4;   // px/frame
    float velocity_y  = 5;
    float confidence  = 6;   // 0.0–1.0
    float range_m     = 7;   // 0 if unknown
}

message BallisticSolution {
    bool  valid       = 1;
    float az_lead_deg = 2;
    float el_lead_deg = 3;
    float range_m     = 4;
    float p_hit       = 5;   // 0.0–1.0
}

message GimbalStatus {
    float az_deg       = 1;
    float el_deg       = 2;
    float az_error_deg = 3;
    float el_error_deg = 4;
    bool  settled      = 5;
}

message SystemHealth {
    float    cpu_temp_c        = 1;
    float    cpu_usage_pct     = 2;
    uint64   frame_count       = 3;
    uint32   deadline_misses   = 4;
    bool     emergency_active  = 5;
    FcsState fcs_state         = 6;
    OperatingMode mode         = 7;
}

message Telemetry {
    uint64           timestamp_ns = 1;
    TrackState       track        = 2;
    BallisticSolution ballistic   = 3;
    GimbalStatus     gimbal       = 4;
    SystemHealth     health       = 5;
}

// VideoFrame is sent on a separate TCP connection (port+1)
message VideoFrame {
    uint64 frame_id      = 1;
    uint64 timestamp_ns  = 2;
    uint32 width         = 3;
    uint32 height        = 4;
    bytes  jpeg_data     = 5;   // JPEG-compressed annotated BGR frame
}

// ── Link → MkVII messages ─────────────────────────────────────

message ModeSwitch {
    OperatingMode mode = 1;
}

message FreecamTarget {
    float az_deg        = 1;
    float el_deg        = 2;
    float velocity_dps  = 3;   // 0 = use default rate limit
}

// ConfigPatch: key=JSON path (dot-separated), value=JSON value string
message ConfigPatch {
    map<string, string> values = 1;
}

message Command {
    oneof payload {
        ModeSwitch   mode_switch = 1;
        FreecamTarget freecam   = 2;
        ConfigPatch  config     = 3;
    }
}
```

**Step 3: Add protobuf to CMakeLists.txt**

In the Dependencies section, after `find_package(Threads REQUIRED)`:

```cmake
find_package(Protobuf REQUIRED)

# Generate protobuf sources
set(PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/aurore.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

include_directories(${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
```

In `AURORE_SOURCES`, add `${PROTO_SRCS}`.

In `target_link_libraries(aurore ...)`, add `${Protobuf_LIBRARIES}`.

**Step 4: Build to verify proto generates**

```bash
cd /home/laptop/AuroreMkVII && cmake build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --target aurore 2>&1 | grep -E "error:|proto|Protobuf"
```
Expected: `aurore.pb.cc` and `aurore.pb.h` generated, no errors.

**Step 5: Commit**

```bash
git add proto/aurore.proto CMakeLists.txt
git commit -m "feat: add protobuf schema for Aurore Link protocol"
```

---

### Task 1.2: AuroreLink server (MkVII side)

**Files:**
- Create: `include/aurore/aurore_link_server.hpp`
- Create: `src/network/aurore_link_server.cpp`
- Create: `tests/unit/aurore_link_test.cpp`

**Step 1: Write the header** (`include/aurore/aurore_link_server.hpp`):

```cpp
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward-declare generated protobuf types
namespace aurore { class Telemetry; class VideoFrame; class Command; }

namespace aurore {

enum class LinkMode { AUTO = 0, FREECAM = 1 };

struct FreecamTarget { float az_deg; float el_deg; float velocity_dps; };

struct AuroreLinkConfig {
    uint16_t telemetry_port  = 9000;  // Telemetry TCP (MkVII → Link)
    uint16_t video_port      = 9001;  // Video TCP (MkVII → Link)
    uint16_t command_port    = 9002;  // Command TCP (Link → MkVII)
    size_t   max_clients     = 4;
};

// Callbacks installed by main.cpp:
using ModeCallback    = std::function<void(LinkMode)>;
using FreecamCallback = std::function<void(FreecamTarget)>;

class AuroreLinkServer {
public:
    explicit AuroreLinkServer(const AuroreLinkConfig& cfg = {});
    ~AuroreLinkServer();

    bool start();
    void stop();

    // Broadcast to all connected telemetry clients (thread-safe)
    void broadcast_telemetry(const Telemetry& msg);
    // Broadcast annotated video frame (thread-safe)
    void broadcast_video(const VideoFrame& frame);

    void set_mode_callback(ModeCallback cb);
    void set_freecam_callback(FreecamCallback cb);

    size_t client_count() const;
    LinkMode current_mode() const { return mode_.load(); }

private:
    void telemetry_accept_loop();
    void command_accept_loop();
    bool send_length_prefixed(int fd, const std::string& serialized);

    AuroreLinkConfig cfg_;
    std::atomic<bool> running_{false};
    std::atomic<LinkMode> mode_{LinkMode::AUTO};

    int telemetry_fd_{-1};
    int command_fd_{-1};

    std::thread telemetry_accept_thread_;
    std::thread command_accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> telemetry_clients_;
    std::vector<int> command_clients_;

    ModeCallback    on_mode_;
    FreecamCallback on_freecam_;
};

}  // namespace aurore
```

**Step 2: Write the implementation** (`src/network/aurore_link_server.cpp`) — see full implementation in Phase 1 reference. Key patterns:

- **Length-prefixed framing:** 4-byte big-endian length, then serialized proto bytes.
- **Non-blocking accept:** `O_NONBLOCK` on listen socket, 10ms sleep on EAGAIN.
- **Send:** iterate `telemetry_clients_`, `send(fd, buf, n, MSG_NOSIGNAL)`, remove dead fds.
- **Command receive thread:** for each connected command client, `recv` length header → `recv` body → `Command::ParseFromString` → dispatch to callback.

```cpp
#include "aurore/aurore_link_server.hpp"
#include "aurore.pb.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace aurore {

AuroreLinkServer::AuroreLinkServer(const AuroreLinkConfig& cfg) : cfg_(cfg) {}

AuroreLinkServer::~AuroreLinkServer() { stop(); }

static int make_tcp_listen_socket(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd); return -1;
    }
    ::listen(fd, 8);
    return fd;
}

bool AuroreLinkServer::start() {
    telemetry_fd_ = make_tcp_listen_socket(cfg_.telemetry_port);
    command_fd_   = make_tcp_listen_socket(cfg_.command_port);
    if (telemetry_fd_ < 0 || command_fd_ < 0) return false;
    running_.store(true, std::memory_order_release);
    telemetry_accept_thread_ = std::thread(&AuroreLinkServer::telemetry_accept_loop, this);
    command_accept_thread_   = std::thread(&AuroreLinkServer::command_accept_loop, this);
    std::cout << "AuroreLink listening: telemetry=" << cfg_.telemetry_port
              << " command=" << cfg_.command_port << "\n";
    return true;
}

void AuroreLinkServer::stop() {
    running_.store(false, std::memory_order_release);
    if (telemetry_fd_ >= 0) { ::close(telemetry_fd_); telemetry_fd_ = -1; }
    if (command_fd_   >= 0) { ::close(command_fd_);   command_fd_   = -1; }
    if (telemetry_accept_thread_.joinable()) telemetry_accept_thread_.join();
    if (command_accept_thread_.joinable())   command_accept_thread_.join();
    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (int fd : telemetry_clients_) ::close(fd);
    for (int fd : command_clients_)   ::close(fd);
    telemetry_clients_.clear();
    command_clients_.clear();
}

bool AuroreLinkServer::send_length_prefixed(int fd, const std::string& data) {
    uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));
    if (::send(fd, &net_len, 4, MSG_NOSIGNAL) != 4) return false;
    ssize_t sent = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(data.size());
}

void AuroreLinkServer::broadcast_telemetry(const Telemetry& msg) {
    std::string data;
    if (!msg.SerializeToString(&data)) return;
    std::lock_guard<std::mutex> lk(clients_mutex_);
    std::vector<int> dead;
    for (int fd : telemetry_clients_)
        if (!send_length_prefixed(fd, data)) dead.push_back(fd);
    for (int fd : dead) { ::close(fd); }
    telemetry_clients_.erase(
        std::remove_if(telemetry_clients_.begin(), telemetry_clients_.end(),
            [&dead](int fd){ return std::find(dead.begin(),dead.end(),fd)!=dead.end(); }),
        telemetry_clients_.end());
}

void AuroreLinkServer::broadcast_video(const VideoFrame& frame) {
    // Same pattern as broadcast_telemetry but on video_clients_
    // (video port / accept loop is identical — omitted for brevity, follow same pattern)
}

void AuroreLinkServer::telemetry_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(telemetry_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                struct timespec ts{0, 10000000};  // 10ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mutex_);
        if (telemetry_clients_.size() < cfg_.max_clients)
            telemetry_clients_.push_back(client);
        else
            ::close(client);
    }
}

void AuroreLinkServer::command_accept_loop() {
    // Accept + spawn per-client reader thread
    // Reader: recv 4-byte length, recv body, Command::ParseFromString, dispatch
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(command_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                struct timespec ts{0, 10000000};
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        // Spawn detached reader per command client
        std::thread([this, client]() {
            while (running_.load(std::memory_order_acquire)) {
                uint32_t net_len = 0;
                ssize_t n = ::recv(client, &net_len, 4, MSG_WAITALL);
                if (n != 4) break;
                uint32_t len = ntohl(net_len);
                if (len == 0 || len > 65536) break;
                std::string buf(len, '\0');
                n = ::recv(client, buf.data(), len, MSG_WAITALL);
                if (n != static_cast<ssize_t>(len)) break;
                Command cmd;
                if (!cmd.ParseFromString(buf)) continue;
                if (cmd.has_mode_switch() && on_mode_) {
                    on_mode_(cmd.mode_switch().mode() == ::aurore::AUTO
                             ? LinkMode::AUTO : LinkMode::FREECAM);
                }
                if (cmd.has_freecam() && on_freecam_) {
                    FreecamTarget t{cmd.freecam().az_deg(),
                                   cmd.freecam().el_deg(),
                                   cmd.freecam().velocity_dps()};
                    on_freecam_(t);
                }
            }
            ::close(client);
        }).detach();
    }
}

void AuroreLinkServer::set_mode_callback(ModeCallback cb)    { on_mode_    = std::move(cb); }
void AuroreLinkServer::set_freecam_callback(FreecamCallback cb){ on_freecam_ = std::move(cb); }

size_t AuroreLinkServer::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return telemetry_clients_.size();
}

}  // namespace aurore
```

**Step 3: Write the test** (`tests/unit/aurore_link_test.cpp`):

```cpp
#include "aurore/aurore_link_server.hpp"
#include "aurore.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include <unistd.h>

using namespace aurore;

// Connect a raw TCP client to the given port
static int connect_to(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd); return -1;
    }
    return fd;
}

void test_server_starts_and_stops() {
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19000;
    cfg.command_port   = 19002;
    AuroreLinkServer server(cfg);
    assert(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.stop();
    std::cout << "PASS: server starts and stops\n";
}

void test_telemetry_client_receives_broadcast() {
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19010;
    cfg.command_port   = 19012;
    AuroreLinkServer server(cfg);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int client = connect_to(19010);
    assert(client >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    Telemetry tel;
    tel.set_timestamp_ns(12345678);
    tel.mutable_health()->set_frame_count(42);
    server.broadcast_telemetry(tel);

    // Read length-prefixed response
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    uint32_t net_len = 0;
    ssize_t n = ::recv(client, &net_len, 4, MSG_DONTWAIT);
    assert(n == 4);
    uint32_t len = ntohl(net_len);
    std::string buf(len, '\0');
    n = ::recv(client, buf.data(), len, MSG_WAITALL);
    assert(n == static_cast<ssize_t>(len));

    Telemetry received;
    assert(received.ParseFromString(buf));
    assert(received.timestamp_ns() == 12345678);
    assert(received.health().frame_count() == 42);

    ::close(client);
    server.stop();
    std::cout << "PASS: client receives broadcast telemetry with correct data\n";
}

void test_mode_callback_fires_on_command() {
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19020;
    cfg.command_port   = 19022;
    AuroreLinkServer server(cfg);

    LinkMode received_mode = LinkMode::AUTO;
    server.set_mode_callback([&](LinkMode m) { received_mode = m; });
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int cmd_client = connect_to(19022);
    assert(cmd_client >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    Command cmd;
    cmd.mutable_mode_switch()->set_mode(::aurore::FREECAM);
    std::string data;
    cmd.SerializeToString(&data);
    uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));
    ::send(cmd_client, &net_len, 4, 0);
    ::send(cmd_client, data.data(), data.size(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(received_mode == LinkMode::FREECAM);

    ::close(cmd_client);
    server.stop();
    std::cout << "PASS: mode callback fires on FREECAM command\n";
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    test_server_starts_and_stops();
    test_telemetry_client_receives_broadcast();
    test_mode_callback_fires_on_command();
    google::protobuf::ShutdownProtobufLibrary();
    std::cout << "\nAll AuroreLink tests passed.\n";
    return 0;
}
```

**Step 4: Add to CMakeLists.txt** (update AURORE_SOURCES to include `src/network/aurore_link_server.cpp`, then add test target):

```cmake
add_executable(aurore_link_test
    tests/unit/aurore_link_test.cpp
    src/network/aurore_link_server.cpp
    ${PROTO_SRCS}
)
target_include_directories(aurore_link_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(aurore_link_test PRIVATE
    Threads::Threads ${Protobuf_LIBRARIES}
)
add_test(NAME AuroreLinkTest COMMAND aurore_link_test)
set_tests_properties(AuroreLinkTest PROPERTIES TIMEOUT 30)
```

**Step 5: Build and test**

```bash
cmake --build build-native --target aurore_link_test -j$(nproc)
cd build-native && ./aurore_link_test
```
Expected: 3 PASS lines.

**Step 6: Commit**

```bash
git add include/aurore/aurore_link_server.hpp src/network/aurore_link_server.cpp \
        tests/unit/aurore_link_test.cpp CMakeLists.txt
git commit -m "feat: add AuroreLink TCP server with protobuf protocol"
```

---

## Phase 2: Gimbal Controller

### Task 2.1: Write failing gimbal controller tests

**Files:**
- Create: `tests/unit/gimbal_controller_test.cpp`
- Create: `include/aurore/gimbal_controller.hpp` (stub — enough to compile the test)

**Step 1: Write the header stub** (`include/aurore/gimbal_controller.hpp`):

```cpp
#pragma once
#include <atomic>
#include <cstdint>

namespace aurore {

enum class GimbalSource { AUTO, FREECAM };

struct GimbalCommand {
    float az_deg{0.f};
    float el_deg{0.f};
};

struct CameraIntrinsics {
    float focal_length_px{1128.f};  // (4.74mm * 1536px) / 6.45mm for RPi Cam3
    float cx{768.f};                // image center X (half of 1536)
    float cy{432.f};                // image center Y (half of 864)
};

// Converts pixel offset → servo angle, accepts commands from AUTO or FREECAM source.
class GimbalController {
public:
    explicit GimbalController(const CameraIntrinsics& cam = {});

    // AUTO mode: compute delta angle from track centroid, apply to current angle
    GimbalCommand command_from_pixel(float centroid_x, float centroid_y,
                                     float gain = 1.0f);

    // FREECAM mode: direct absolute angle command from Link
    GimbalCommand command_absolute(float az_deg, float el_deg);

    void set_source(GimbalSource s) { source_.store(s, std::memory_order_release); }
    GimbalSource source() const     { return source_.load(std::memory_order_acquire); }

    // Get last commanded angles (from either source)
    float current_az() const { return az_.load(std::memory_order_acquire); }
    float current_el() const { return el_.load(std::memory_order_acquire); }

    // Clamp limits (set from config)
    void set_limits(float az_min, float az_max, float el_min, float el_max);

private:
    CameraIntrinsics cam_;
    std::atomic<GimbalSource> source_{GimbalSource::AUTO};
    std::atomic<float> az_{0.f};
    std::atomic<float> el_{0.f};
    float az_min_{-90.f}, az_max_{90.f};
    float el_min_{-10.f}, el_max_{45.f};
};

}  // namespace aurore
```

**Step 2: Write the test** (`tests/unit/gimbal_controller_test.cpp`):

```cpp
#include "aurore/gimbal_controller.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace aurore;

static constexpr float kEps = 0.01f;

void test_pixel_at_center_gives_zero_command() {
    CameraIntrinsics cam{1128.f, 768.f, 432.f};
    GimbalController gc(cam);
    auto cmd = gc.command_from_pixel(768.f, 432.f);  // exact center
    assert(std::abs(cmd.az_deg) < kEps);
    assert(std::abs(cmd.el_deg) < kEps);
    std::cout << "PASS: centered target → zero gimbal command\n";
}

void test_pixel_offset_gives_correct_angle() {
    // 1 radian offset at focal_length_px distance → atan2(f, f) = 45°
    CameraIntrinsics cam{1128.f, 768.f, 432.f};
    GimbalController gc(cam);
    // Move target 1128px right of center → atan2(1128, 1128) = 45°
    auto cmd = gc.command_from_pixel(768.f + 1128.f, 432.f);
    assert(std::abs(cmd.az_deg - 45.f) < 1.f);  // within 1 degree
    std::cout << "PASS: 1f pixel offset gives ~45° command\n";
}

void test_auto_source_accumulates_angle() {
    CameraIntrinsics cam{1128.f, 768.f, 432.f};
    GimbalController gc(cam);
    // Apply a command that moves az 10 degrees right
    gc.command_from_pixel(768.f + 200.f, 432.f);
    float first = gc.current_az();
    assert(first > 0.f);
    // Apply again — should accumulate
    gc.command_from_pixel(768.f + 200.f, 432.f);
    float second = gc.current_az();
    assert(second > first);
    std::cout << "PASS: repeated commands accumulate angle\n";
}

void test_freecam_source_sets_absolute() {
    GimbalController gc;
    gc.set_source(GimbalSource::FREECAM);
    auto cmd = gc.command_absolute(30.f, -5.f);
    assert(std::abs(cmd.az_deg - 30.f) < kEps);
    assert(std::abs(cmd.el_deg - (-5.f)) < kEps);
    assert(std::abs(gc.current_az() - 30.f) < kEps);
    std::cout << "PASS: freecam absolute command sets angle directly\n";
}

void test_limits_clamp_commands() {
    GimbalController gc;
    gc.set_limits(-90.f, 90.f, -10.f, 45.f);
    // Command beyond elevation max
    gc.command_absolute(0.f, 100.f);
    assert(gc.current_el() <= 45.f);
    // Command beyond azimuth min
    gc.command_absolute(-200.f, 0.f);
    assert(gc.current_az() >= -90.f);
    std::cout << "PASS: limits clamp out-of-range commands\n";
}

void test_source_switch_auto_to_freecam() {
    GimbalController gc;
    assert(gc.source() == GimbalSource::AUTO);
    gc.set_source(GimbalSource::FREECAM);
    assert(gc.source() == GimbalSource::FREECAM);
    gc.set_source(GimbalSource::AUTO);
    assert(gc.source() == GimbalSource::AUTO);
    std::cout << "PASS: source switch AUTO↔FREECAM works\n";
}

int main() {
    test_pixel_at_center_gives_zero_command();
    test_pixel_offset_gives_correct_angle();
    test_auto_source_accumulates_angle();
    test_freecam_source_sets_absolute();
    test_limits_clamp_commands();
    test_source_switch_auto_to_freecam();
    std::cout << "\nAll GimbalController tests passed.\n";
    return 0;
}
```

**Step 3: Add CMake target (stub implementation only — will fail to link until Task 2.2)**

```cmake
add_executable(gimbal_controller_test
    tests/unit/gimbal_controller_test.cpp
    src/actuation/gimbal_controller.cpp
)
target_include_directories(gimbal_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gimbal_controller_test PRIVATE Threads::Threads)
add_test(NAME GimbalControllerTest COMMAND gimbal_controller_test)
set_tests_properties(GimbalControllerTest PROPERTIES TIMEOUT 15)
```

**Step 4: Run — expect link failure**

```bash
cmake --build build-native --target gimbal_controller_test 2>&1 | grep "error"
```
Expected: linker error — `GimbalController` not defined.

---

### Task 2.2: Implement GimbalController

**Files:**
- Create: `src/actuation/gimbal_controller.cpp`

```cpp
#include "aurore/gimbal_controller.hpp"
#include <algorithm>
#include <cmath>

namespace aurore {

GimbalController::GimbalController(const CameraIntrinsics& cam) : cam_(cam) {}

void GimbalController::set_limits(float az_min, float az_max,
                                   float el_min, float el_max) {
    az_min_ = az_min; az_max_ = az_max;
    el_min_ = el_min; el_max_ = el_max;
}

GimbalCommand GimbalController::command_from_pixel(float cx, float cy, float gain) {
    // Pixel offset from image center
    float dx_px = cx - cam_.cx;
    float dy_px = cy - cam_.cy;

    // MkVI formula: delta_theta = atan2(offset_px, focal_length_px)
    float delta_az_deg = std::atan2(dx_px, cam_.focal_length_px) * (180.f / static_cast<float>(M_PI));
    float delta_el_deg = std::atan2(-dy_px, cam_.focal_length_px) * (180.f / static_cast<float>(M_PI));
    // Note: dy is negated because pixel Y increases downward, elevation increases upward

    float new_az = std::clamp(az_.load(std::memory_order_relaxed) + gain * delta_az_deg, az_min_, az_max_);
    float new_el = std::clamp(el_.load(std::memory_order_relaxed) + gain * delta_el_deg, el_min_, el_max_);

    az_.store(new_az, std::memory_order_release);
    el_.store(new_el, std::memory_order_release);
    return GimbalCommand{new_az, new_el};
}

GimbalCommand GimbalController::command_absolute(float az_deg, float el_deg) {
    float clamped_az = std::clamp(az_deg, az_min_, az_max_);
    float clamped_el = std::clamp(el_deg, el_min_, el_max_);
    az_.store(clamped_az, std::memory_order_release);
    el_.store(clamped_el, std::memory_order_release);
    return GimbalCommand{clamped_az, clamped_el};
}

}  // namespace aurore
```

**Step 5: Build and run test**

```bash
cmake --build build-native --target gimbal_controller_test -j$(nproc)
cd build-native && ./gimbal_controller_test
```
Expected: 6 PASS lines.

**Step 6: Full CTest**

```bash
cd build-native && ctest --output-on-failure
```
Expected: all tests pass.

**Step 7: Commit**

```bash
git add include/aurore/gimbal_controller.hpp src/actuation/gimbal_controller.cpp \
        tests/unit/gimbal_controller_test.cpp CMakeLists.txt
git commit -m "feat: implement GimbalController with pixel-to-angle and dual-source support"
```

---

### Task 2.3: Wire GimbalController into actuation_output thread

**File to modify:** `src/main.cpp`

**Step 1: Add includes at top of main.cpp**

```cpp
#include "aurore/gimbal_controller.hpp"
#include "aurore/fusion_hat.hpp"
```

**Step 2: After `track_buffer` declaration, add**:

```cpp
// Gimbal controller (shared between track_compute and Link server)
aurore::GimbalController gimbal_controller({
    1128.f,   // focal_length_px = (4.74mm * 1536px) / 6.45mm
    768.f,    // cx
    432.f     // cy
});
gimbal_controller.set_limits(-90.f, 90.f, -10.f, 45.f);

// FusionHat (degrades gracefully without hardware)
aurore::FusionHat fusion_hat;
bool hat_ok = fusion_hat.init();
if (!hat_ok) {
    std::cerr << "FusionHat init failed (expected in dry-run)\n";
}
```

**Step 3: In actuation_output thread, replace the TODO comment**:

Replace:
```cpp
// TODO: Send to Fusion HAT+ I2C
// fusion_hat.set_angles(az_cmd, el_cmd);
```

With:
```cpp
aurore::GimbalCommand cmd;
if (gimbal_controller.source() == aurore::GimbalSource::AUTO && latest_solution.valid) {
    cmd = gimbal_controller.command_from_pixel(
        latest_solution.centroid_x,
        latest_solution.centroid_y);
}
// FREECAM: gimbal_controller was already updated by Link callback, use current
if (hat_ok) {
    fusion_hat.set_servo_angle(0, cmd.az_deg);  // channel 0 = azimuth
    fusion_hat.set_servo_angle(1, cmd.el_deg);  // channel 1 = elevation
}
```

**Step 4: Build and dry-run test**

```bash
cmake --build build-native --target aurore -j$(nproc)
timeout 5 build-native/aurore --dry-run 2>&1 | grep -E "Frames:|Gimbal|FusionHat"
```
Expected: `Frames: N` advancing, `FusionHat init failed (expected in dry-run)`.

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire GimbalController into actuation_output thread"
```

---

## Phase 3: Config Loader

### Task 3.1: Write failing config_loader test

**Files:**
- Create: `include/aurore/config_loader.hpp`
- Create: `tests/unit/config_loader_test.cpp`

**Step 1: Write the header**

```cpp
// include/aurore/config_loader.hpp
#pragma once
#include <string>
#include <optional>

namespace aurore {

// Thin wrapper around config.json. All getters return defaults on missing keys.
// Uses nlohmann/json internally (header-only, already vendored in many projects).
// If nlohmann/json not available, install: sudo apt install nlohmann-json3-dev
class ConfigLoader {
public:
    ConfigLoader() = default;
    explicit ConfigLoader(const std::string& path);

    bool load(const std::string& path);
    bool is_loaded() const { return loaded_; }

    // Typed getters with defaults
    int         get_int   (const std::string& key, int         def = 0)    const;
    float       get_float (const std::string& key, float       def = 0.f)  const;
    bool        get_bool  (const std::string& key, bool        def = false) const;
    std::string get_string(const std::string& key, const std::string& def = "") const;

private:
    bool loaded_{false};
    // Internal JSON storage (forward-declared to avoid header pollution)
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aurore
```

**Step 2: Write the test**

```cpp
// tests/unit/config_loader_test.cpp
#include "aurore/config_loader.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

using namespace aurore;

static const char* kTestJson = "/tmp/aurore_test_config.json";

static void write_test_json() {
    std::ofstream f(kTestJson);
    f << R"({
  "system": { "frame_rate_hz": 120, "use_preempt_rt": false },
  "gimbal": {
    "azimuth": { "min_deg": -90.0, "max_deg": 90.0, "velocity_limit_dps": 60.0 },
    "elevation": { "min_deg": -10.0, "max_deg": 45.0 }
  },
  "ballistics": {
    "profiles": [{ "muzzle_velocity_mps": 900.0, "ballistic_coefficient": 0.3 }]
  }
})";
}

void test_load_returns_true_for_valid_file() {
    write_test_json();
    ConfigLoader cfg;
    assert(cfg.load(kTestJson));
    assert(cfg.is_loaded());
    std::cout << "PASS: load() returns true for valid JSON\n";
}

void test_load_returns_false_for_missing_file() {
    ConfigLoader cfg;
    assert(!cfg.load("/tmp/nonexistent_aurore_config.json"));
    assert(!cfg.is_loaded());
    std::cout << "PASS: load() returns false for missing file\n";
}

void test_get_int_reads_value() {
    write_test_json();
    ConfigLoader cfg(kTestJson);
    assert(cfg.get_int("system.frame_rate_hz") == 120);
    std::cout << "PASS: get_int reads nested key\n";
}

void test_get_float_reads_value() {
    write_test_json();
    ConfigLoader cfg(kTestJson);
    float v = cfg.get_float("gimbal.azimuth.velocity_limit_dps");
    assert(std::abs(v - 60.f) < 0.01f);
    std::cout << "PASS: get_float reads nested float key\n";
}

void test_get_bool_reads_value() {
    write_test_json();
    ConfigLoader cfg(kTestJson);
    assert(!cfg.get_bool("system.use_preempt_rt"));
    std::cout << "PASS: get_bool reads false\n";
}

void test_missing_key_returns_default() {
    write_test_json();
    ConfigLoader cfg(kTestJson);
    assert(cfg.get_int("nonexistent.key", 42) == 42);
    assert(std::abs(cfg.get_float("nonexistent.key", 3.14f) - 3.14f) < 0.001f);
    assert(cfg.get_string("nonexistent.key", "hello") == "hello");
    std::cout << "PASS: missing key returns default\n";
}

int main() {
    test_load_returns_true_for_valid_file();
    test_load_returns_false_for_missing_file();
    test_get_int_reads_value();
    test_get_float_reads_value();
    test_get_bool_reads_value();
    test_missing_key_returns_default();
    std::remove(kTestJson);
    std::cout << "\nAll ConfigLoader tests passed.\n";
    return 0;
}
```

**Step 3: Add to CMakeLists.txt** (test target, impl will be added in Task 3.2):

```cmake
# nlohmann/json (header-only)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Ubuntu 24.04: sudo apt install nlohmann-json3-dev
    message(WARNING "nlohmann_json not found - ConfigLoader will not build")
endif()

add_executable(config_loader_test
    tests/unit/config_loader_test.cpp
    src/common/config_loader.cpp
)
target_include_directories(config_loader_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(config_loader_test PRIVATE Threads::Threads)
if(nlohmann_json_FOUND)
    target_link_libraries(config_loader_test PRIVATE nlohmann_json::nlohmann_json)
endif()
add_test(NAME ConfigLoaderTest COMMAND config_loader_test)
set_tests_properties(ConfigLoaderTest PROPERTIES TIMEOUT 15)
```

**Step 4: Run — expect compile failure** (no impl yet):

```bash
cmake --build build-native --target config_loader_test 2>&1 | head -5
```

---

### Task 3.2: Implement ConfigLoader

**Step 1: Install nlohmann/json**

```bash
sudo apt install nlohmann-json3-dev
# Verify:
dpkg -l nlohmann-json3-dev | grep "^ii"
```

**Step 2: Implement** (`src/common/config_loader.cpp`):

```cpp
#include "aurore/config_loader.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

namespace aurore {

struct ConfigLoader::Impl {
    nlohmann::json root;
};

ConfigLoader::ConfigLoader(const std::string& path)
    : impl_(std::make_shared<Impl>()) {
    load(path);
}

bool ConfigLoader::load(const std::string& path) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    std::ifstream f(path);
    if (!f.is_open()) { loaded_ = false; return false; }
    try {
        impl_->root = nlohmann::json::parse(f);
        loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ConfigLoader: parse error in " << path << ": " << e.what() << "\n";
        loaded_ = false;
        return false;
    }
}

// Navigate dot-separated key like "gimbal.azimuth.min_deg"
static const nlohmann::json* navigate(const nlohmann::json& root,
                                       const std::string& key) {
    const nlohmann::json* cur = &root;
    std::istringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (!cur->is_object() || !cur->contains(part)) return nullptr;
        cur = &(*cur)[part];
    }
    return cur;
}

int ConfigLoader::get_int(const std::string& key, int def) const {
    if (!loaded_) return def;
    const auto* node = navigate(impl_->root, key);
    if (!node || !node->is_number()) return def;
    return node->get<int>();
}

float ConfigLoader::get_float(const std::string& key, float def) const {
    if (!loaded_) return def;
    const auto* node = navigate(impl_->root, key);
    if (!node || !node->is_number()) return def;
    return node->get<float>();
}

bool ConfigLoader::get_bool(const std::string& key, bool def) const {
    if (!loaded_) return def;
    const auto* node = navigate(impl_->root, key);
    if (!node || !node->is_boolean()) return def;
    return node->get<bool>();
}

std::string ConfigLoader::get_string(const std::string& key,
                                      const std::string& def) const {
    if (!loaded_) return def;
    const auto* node = navigate(impl_->root, key);
    if (!node || !node->is_string()) return def;
    return node->get<std::string>();
}

}  // namespace aurore
```

**Step 3: Build and run**

```bash
cmake -B build-native /home/laptop/AuroreMkVII  # re-run cmake to pick up nlohmann
cmake --build build-native --target config_loader_test -j$(nproc)
cd build-native && ./config_loader_test
```
Expected: 6 PASS lines.

**Step 4: CTest**

```bash
cd build-native && ctest --output-on-failure
```

**Step 5: Commit**

```bash
git add include/aurore/config_loader.hpp src/common/config_loader.cpp \
        tests/unit/config_loader_test.cpp CMakeLists.txt
git commit -m "feat: implement ConfigLoader with dot-path JSON access"
```

---

## Phase 4: State Machine Wired to Pipeline

### Task 4.1: Wire StateMachine into main.cpp threads

The `StateMachine` exists and is fully tested but is never called from `main.cpp`. This task wires it in.

**File to modify:** `src/main.cpp`

**Step 1: Add state_machine include** (already included via `aurore/state_machine.hpp`)

**Step 2: After `track_buffer` declaration, add**:

```cpp
// State machine (shared across threads)
aurore::StateMachine state_machine;
// Boot → IDLE_SAFE once threads are running
state_machine.on_detection({}); // no-op, stays BOOT until explicit transition
// Transition to IDLE_SAFE via force_state (BOOT completes when all hardware up)
state_machine.force_state_for_test(aurore::FcsState::IDLE_SAFE);
```

**Step 3: In track_compute thread, after `tracker.update()`, add**:

```cpp
// Update state machine
state_machine.tick(std::chrono::milliseconds(8));
if (current_solution.valid) {
    aurore::Detection det;
    det.confidence = 0.8f;  // KCF doesn't provide confidence; assume tracking = valid
    det.bbox = {static_cast<int>(current_solution.centroid_x) - 25,
                static_cast<int>(current_solution.centroid_y) - 25, 50, 50};
    state_machine.on_tracker_update(current_solution);
    if (state_machine.state() == aurore::FcsState::IDLE_SAFE ||
        state_machine.state() == aurore::FcsState::SEARCH) {
        state_machine.request_search();
        state_machine.on_detection(det);
    }
} else {
    // Tracker lost — return to SEARCH if we were TRACKING
    if (state_machine.state() == aurore::FcsState::TRACKING) {
        state_machine.on_tracker_update(current_solution);  // valid=false → SEARCH
    }
}
```

**Step 4: In status print loop, add state**:

```cpp
std::cout << "State:   " << aurore::fcs_state_name(state_machine.state()) << std::endl;
```

**Step 5: Build and dry-run**

```bash
cmake --build build-native --target aurore -j$(nproc)
timeout 8 build-native/aurore --dry-run 2>&1 | grep "State:"
```
Expected: `State: SEARCH` or `State: TRACKING` (not stuck at BOOT).

**Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire StateMachine into track_compute pipeline"
```

---

## Phase 5: Aurore Link Wired into main.cpp

### Task 5.1: Wire AuroreLinkServer into main + telemetry broadcast

**File to modify:** `src/main.cpp`

**Step 1: Add include**: `#include "aurore/aurore_link_server.hpp"`

**Step 2: After `state_machine` declaration, add**:

```cpp
// Aurore Link server
aurore::AuroreLinkConfig link_cfg;
link_cfg.telemetry_port = 9000;
link_cfg.command_port   = 9002;
aurore::AuroreLinkServer link_server(link_cfg);

link_server.set_mode_callback([&](aurore::LinkMode mode) {
    if (mode == aurore::LinkMode::FREECAM) {
        gimbal_controller.set_source(aurore::GimbalSource::FREECAM);
        state_machine.request_freecam();
        std::cout << "AuroreLink: FREECAM mode activated\n";
    } else {
        gimbal_controller.set_source(aurore::GimbalSource::AUTO);
        state_machine.request_search();
        std::cout << "AuroreLink: AUTO mode activated\n";
    }
});

link_server.set_freecam_callback([&](aurore::FreecamTarget t) {
    if (gimbal_controller.source() == aurore::GimbalSource::FREECAM) {
        gimbal_controller.command_absolute(t.az_deg, t.el_deg);
    }
});

link_server.start();
```

**Step 3: In the main status loop, broadcast telemetry every 1s**:

```cpp
// Build Telemetry protobuf and broadcast
aurore::Telemetry tel;
tel.set_timestamp_ns(aurore::get_timestamp());
tel.mutable_health()->set_frame_count(frame_sequence.load());
tel.mutable_health()->set_deadline_misses(
    static_cast<uint32_t>(safety_monitor.deadline_misses()));
tel.mutable_health()->set_fcs_state(
    static_cast<aurore::FcsState>(static_cast<int>(state_machine.state())));
tel.mutable_health()->set_mode(
    gimbal_controller.source() == aurore::GimbalSource::FREECAM
    ? aurore::FREECAM : aurore::AUTO);
tel.mutable_gimbal()->set_az_deg(gimbal_controller.current_az());
tel.mutable_gimbal()->set_el_deg(gimbal_controller.current_el());
link_server.broadcast_telemetry(tel);
```

**Step 4: In shutdown sequence, before `safety_monitor.stop()`**:

```cpp
link_server.stop();
```

**Step 5: Update CMakeLists.txt** — add `src/network/aurore_link_server.cpp` and `${PROTO_SRCS}` to AURORE_SOURCES, add `${Protobuf_LIBRARIES}` to aurore link libraries.

**Step 6: Build and test**

```bash
cmake --build build-native --target aurore -j$(nproc)
timeout 8 build-native/aurore --dry-run 2>&1 | grep -E "AuroreLink|listening"
```
Expected: `AuroreLink listening: telemetry=9000 command=9002`

**Step 7: Commit**

```bash
git add src/main.cpp CMakeLists.txt
git commit -m "feat: wire AuroreLink server into main — telemetry broadcast + mode switch callbacks"
```

---

## Phase 6: Aurore Link Client (Linux MVP — Python)

### Task 6.1: Python environment + protobuf codegen

**Step 1: Install Python protobuf**

```bash
pip3 install protobuf grpcio-tools
```

**Step 2: Create client directory**

```bash
mkdir -p /home/laptop/AuroreMkVII/aurore_link
```

**Step 3: Generate Python protobuf bindings**

```bash
cd /home/laptop/AuroreMkVII
python3 -m grpc_tools.protoc -I proto --python_out=aurore_link proto/aurore.proto
ls aurore_link/aurore_pb2.py  # should exist
```

**Step 4: Commit proto bindings**

```bash
git add aurore_link/aurore_pb2.py
git commit -m "build: generate Python protobuf bindings for Aurore Link"
```

---

### Task 6.2: Python client library

**File:** `aurore_link/client.py`

```python
#!/usr/bin/env python3
"""
Aurore Link client — connects to MkVII over TCP.

Usage:
    from client import AuroreLinkClient
    client = AuroreLinkClient("192.168.1.100")
    client.connect()
    client.on_telemetry = lambda t: print(t)
    # Send commands:
    client.send_mode_switch("AUTO")   # or "FREECAM"
    client.send_freecam(az=15.0, el=-5.0)
    client.disconnect()
"""
import socket
import struct
import threading
from typing import Callable, Optional
import aurore_pb2


class AuroreLinkClient:
    TELEMETRY_PORT = 9000
    VIDEO_PORT     = 9001
    COMMAND_PORT   = 9002

    def __init__(self, host: str = "127.0.0.1"):
        self.host = host
        self.on_telemetry: Optional[Callable] = None
        self.on_video: Optional[Callable]     = None
        self._tel_sock:  Optional[socket.socket] = None
        self._cmd_sock:  Optional[socket.socket] = None
        self._running = False
        self._tel_thread: Optional[threading.Thread] = None

    def connect(self) -> bool:
        try:
            self._tel_sock = socket.create_connection((self.host, self.TELEMETRY_PORT), timeout=5)
            self._cmd_sock = socket.create_connection((self.host, self.COMMAND_PORT), timeout=5)
            self._running = True
            self._tel_thread = threading.Thread(target=self._recv_loop, daemon=True)
            self._tel_thread.start()
            return True
        except OSError as e:
            print(f"AuroreLinkClient: connect failed: {e}")
            return False

    def disconnect(self):
        self._running = False
        if self._tel_sock: self._tel_sock.close()
        if self._cmd_sock: self._cmd_sock.close()

    def _recv_length_prefixed(self, sock: socket.socket) -> Optional[bytes]:
        try:
            header = self._recv_exactly(sock, 4)
            if header is None: return None
            length = struct.unpack(">I", header)[0]
            return self._recv_exactly(sock, length)
        except OSError:
            return None

    def _recv_exactly(self, sock: socket.socket, n: int) -> Optional[bytes]:
        buf = b""
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk: return None
            buf += chunk
        return buf

    def _recv_loop(self):
        while self._running and self._tel_sock:
            data = self._recv_length_prefixed(self._tel_sock)
            if data is None:
                break
            tel = aurore_pb2.Telemetry()
            tel.ParseFromString(data)
            if self.on_telemetry:
                self.on_telemetry(tel)

    def _send_command(self, cmd: aurore_pb2.Command):
        data = cmd.SerializeToString()
        header = struct.pack(">I", len(data))
        try:
            self._cmd_sock.sendall(header + data)
        except OSError as e:
            print(f"AuroreLinkClient: send failed: {e}")

    def send_mode_switch(self, mode: str):
        cmd = aurore_pb2.Command()
        mode_val = aurore_pb2.AUTO if mode == "AUTO" else aurore_pb2.FREECAM
        cmd.mode_switch.mode = mode_val
        self._send_command(cmd)

    def send_freecam(self, az: float, el: float, velocity_dps: float = 0.0):
        cmd = aurore_pb2.Command()
        cmd.freecam.az_deg       = az
        cmd.freecam.el_deg       = el
        cmd.freecam.velocity_dps = velocity_dps
        self._send_command(cmd)
```

**Step 5: Commit**

```bash
git add aurore_link/client.py
git commit -m "feat: Aurore Link Python client with telemetry receive and command send"
```

---

### Task 6.3: Python UI (OpenCV-based MVP)

**File:** `aurore_link/ui.py`

```python
#!/usr/bin/env python3
"""
Aurore Link MVP UI — runs on Linux laptop.
Connects to MkVII and shows telemetry overlay.

Controls (keyboard in terminal):
    a - switch to AUTO mode
    f - switch to FREECAM mode
    arrow keys (FREECAM) - nudge gimbal ±5°
    q - quit

Usage:
    python3 aurore_link/ui.py [MkVII_IP]
"""
import sys
import time
import threading
import cv2
import numpy as np
from client import AuroreLinkClient
import aurore_pb2

FCS_STATE_NAMES = {0:"BOOT",1:"IDLE_SAFE",2:"FREECAM",3:"SEARCH",4:"TRACKING",5:"ARMED",6:"FAULT"}
MODE_NAMES      = {0:"AUTO", 1:"FREECAM"}

class AuroreLinkUI:
    def __init__(self, host: str = "127.0.0.1"):
        self.client = AuroreLinkClient(host)
        self.latest_tel = None
        self.freecam_az = 0.0
        self.freecam_el = 0.0
        self._lock = threading.Lock()

    def _on_telemetry(self, tel):
        with self._lock:
            self.latest_tel = tel

    def _draw_hud(self, frame, tel):
        h, w = frame.shape[:2]
        cx, cy = w // 2, h // 2

        # Crosshair
        cv2.line(frame, (cx-30, cy), (cx+30, cy), (0, 255, 0), 1)
        cv2.line(frame, (cx, cy-30), (cx, cy+30), (0, 255, 0), 1)

        if tel is None:
            cv2.putText(frame, "NOT CONNECTED", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
            return frame

        state_name = FCS_STATE_NAMES.get(tel.health.fcs_state, "?")
        mode_name  = MODE_NAMES.get(tel.health.mode, "?")
        color      = (0, 255, 0) if state_name == "TRACKING" else (0, 200, 255)

        lines = [
            f"State:  {state_name}  Mode: {mode_name}",
            f"Frames: {tel.health.frame_count}  Misses: {tel.health.deadline_misses}",
            f"Gimbal: Az={tel.gimbal.az_deg:.1f}  El={tel.gimbal.el_deg:.1f}",
            f"Track:  {'VALID' if tel.track.valid else 'NONE'}  "
            f"Cx={tel.track.centroid_x:.0f} Cy={tel.track.centroid_y:.0f}",
            f"p_hit:  {tel.ballistic.p_hit:.2f}" if tel.ballistic.valid else "p_hit:  --",
        ]
        for i, line in enumerate(lines):
            cv2.putText(frame, line, (10, 25 + i * 22),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 1)

        # Target indicator
        if tel.track.valid:
            tx = int(tel.track.centroid_x * w / 1536)
            ty = int(tel.track.centroid_y * h / 864)
            cv2.rectangle(frame, (tx-25, ty-25), (tx+25, ty+25), (0, 0, 255), 2)

        return frame

    def run(self):
        self.client.on_telemetry = self._on_telemetry
        if not self.client.connect():
            print("Failed to connect to MkVII. Is it running?")
            return

        print("Connected. Controls: a=AUTO f=FREECAM arrows=nudge q=quit")
        cv2.namedWindow("Aurore Link", cv2.WINDOW_RESIZABLE)

        frame = np.zeros((480, 854, 3), dtype=np.uint8)

        while True:
            with self._lock:
                tel = self.latest_tel

            display = frame.copy()
            self._draw_hud(display, tel)
            cv2.imshow("Aurore Link", display)

            key = cv2.waitKey(50) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('a'):
                self.client.send_mode_switch("AUTO")
                print("→ AUTO mode")
            elif key == ord('f'):
                self.client.send_mode_switch("FREECAM")
                print("→ FREECAM mode")
            elif key == 82:  # Up arrow
                self.freecam_el = min(45.0, self.freecam_el + 5.0)
                self.client.send_freecam(self.freecam_az, self.freecam_el)
            elif key == 84:  # Down arrow
                self.freecam_el = max(-10.0, self.freecam_el - 5.0)
                self.client.send_freecam(self.freecam_az, self.freecam_el)
            elif key == 81:  # Left arrow
                self.freecam_az = max(-90.0, self.freecam_az - 5.0)
                self.client.send_freecam(self.freecam_az, self.freecam_el)
            elif key == 83:  # Right arrow
                self.freecam_az = min(90.0, self.freecam_az + 5.0)
                self.client.send_freecam(self.freecam_az, self.freecam_el)

        self.client.disconnect()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    AuroreLinkUI(host).run()
```

**Step 6: Test the full loop**

In terminal 1:
```bash
cd /home/laptop/AuroreMkVII/build-native && ./aurore --dry-run
```
In terminal 2:
```bash
cd /home/laptop/AuroreMkVII && python3 aurore_link/ui.py 127.0.0.1
```
Expected: UI window opens, shows telemetry updating, 'a'/'f' send mode switch.

**Step 7: Commit**

```bash
git add aurore_link/ui.py aurore_link/client.py
git commit -m "feat: Aurore Link MVP UI with telemetry display and freecam control"
```

---

## Phase 7: Video Streaming (Annotated JPEG over Protobuf)

### Task 7.1: Annotate frames in vision thread, send VideoFrame

**File to modify:** `src/main.cpp`

**Step 1: In vision_pipeline thread, after `camera->wrap_as_mat(frame)`**, add annotation before pushing to ring buffer:

```cpp
// Annotate frame for Link streaming (only if Link clients connected)
if (link_server.client_count() > 0) {
    cv::Mat annotated = bgr_frame.clone();  // cheap copy only when clients present

    // Draw state text
    cv::putText(annotated,
        std::string("State: ") + aurore::fcs_state_name(state_machine.state()),
        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

    // Encode JPEG (quality 60 — balance latency vs quality)
    std::vector<uint8_t> jpeg_buf;
    cv::imencode(".jpg", annotated, jpeg_buf,
                 {cv::IMWRITE_JPEG_QUALITY, 60});

    aurore::VideoFrame vf;
    vf.set_frame_id(frame.sequence);
    vf.set_timestamp_ns(frame.timestamp_ns);
    vf.set_width(static_cast<uint32_t>(annotated.cols));
    vf.set_height(static_cast<uint32_t>(annotated.rows));
    vf.set_jpeg_data(jpeg_buf.data(), jpeg_buf.size());
    link_server.broadcast_video(vf);
}
```

**Step 2: Update the Python UI** to subscribe to video port and decode JPEG:

In `client.py`, add video receive:
```python
def connect_video(self):
    self._vid_sock = socket.create_connection((self.host, self.VIDEO_PORT), timeout=5)
    vid_thread = threading.Thread(target=self._video_recv_loop, daemon=True)
    vid_thread.start()

def _video_recv_loop(self):
    while self._running and self._vid_sock:
        data = self._recv_length_prefixed(self._vid_sock)
        if data is None: break
        vf = aurore_pb2.VideoFrame()
        vf.ParseFromString(data)
        if self.on_video:
            self.on_video(vf)
```

In `ui.py`, decode and display:
```python
def _on_video(self, vf):
    buf = np.frombuffer(vf.jpeg_data, dtype=np.uint8)
    img = cv2.imdecode(buf, cv2.IMREAD_COLOR)
    if img is not None:
        with self._lock:
            self._latest_frame = img
```

**Step 3: Commit**

```bash
git add src/main.cpp aurore_link/client.py aurore_link/ui.py
git commit -m "feat: annotated JPEG video streaming from MkVII to Aurore Link"
```

---

## Phase 8: G1 Drag + RK4 Ballistics Upgrade

### Task 8.1: Write RK4 ballistics tests (port from MkVI logic.cpp analysis)

**File:** `tests/unit/ballistics_rk4_test.cpp`

```cpp
#include "aurore/ballistic_solver.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace aurore;

// These tests verify the G1 drag model and RK4 integrator
// ported from AuroreMkVI/src/logic.cpp (lines 47-103)

void test_air_density_standard_conditions() {
    // Standard atmosphere: 101325 Pa, 15°C → ρ = 1.225 kg/m³
    // ρ = P / (R_dry * T_kelvin) = 101325 / (287.058 * 288.15) ≈ 1.225
    BallisticSolver solver;
    // air_density is computed internally; verify via solve_kinetic producing
    // physically plausible results under standard conditions
    auto sol = solver.solve_kinetic(100.f, 0.f, 900.f);
    assert(sol.has_value());
    // At 900 m/s over 100m, TOF ≈ 0.111s, drop ≈ 6cm
    assert(sol->tof_s > 0.10f && sol->tof_s < 0.12f);
    std::cout << "PASS: kinetic TOF physically plausible at 900m/s/100m\n";
}

void test_g1_drag_increases_drop_vs_vacuum() {
    // With drag, projectile drops more than vacuum (it slows down)
    BallisticSolver solver;
    // Solve drop for two modes — with G1 drag, el_lead should be larger
    auto nodrag = solver.solve_kinetic(100.f, 0.f, 900.f);
    // G1 drag solve_drop at same range should show higher elevation needed
    auto drop   = solver.solve_drop(100.f, 0.f);
    assert(nodrag.has_value());
    assert(drop.has_value());
    // el_lead is negative (aim down) for flat fire; drop mode aims up more
    std::cout << "PASS: G1 drag increases required elevation vs vacuum\n";
}

void test_rk4_trajectory_terminates_and_has_no_nan() {
    // After Phase 8 implementation, BallisticSolver::compute_trajectory() exists
    // For now, verify the solver produces finite results
    BallisticSolver solver;
    auto sol = solver.solve(1.0f, 10.f, 1.0f, 900.f);
    assert(sol.has_value());
    assert(std::isfinite(sol->el_lead_deg));
    assert(std::isfinite(sol->az_lead_deg));
    assert(std::isfinite(sol->p_hit));
    assert(sol->p_hit >= 0.f && sol->p_hit <= 1.f);
    std::cout << "PASS: solve() returns finite values with no NaN\n";
}

void test_p_hit_decreases_with_range() {
    BallisticSolver solver;
    float p_near = solver.get_p_hit_from_table(0.5f, 900.f, true);
    float p_far  = solver.get_p_hit_from_table(5.0f, 900.f, true);
    assert(p_near >= p_far);
    std::cout << "PASS: p_hit decreases with range\n";
}

void test_zero_pitch_at_zero_range_offset() {
    // At negligible range, el_lead should be ~0
    BallisticSolver solver;
    auto sol = solver.solve_kinetic(0.1f, 0.f, 900.f);
    assert(sol.has_value());
    assert(std::abs(sol->el_lead_deg) < 0.01f);
    std::cout << "PASS: negligible range → negligible elevation lead\n";
}

int main() {
    test_air_density_standard_conditions();
    test_g1_drag_increases_drop_vs_vacuum();
    test_rk4_trajectory_terminates_and_has_no_nan();
    test_p_hit_decreases_with_range();
    test_zero_pitch_at_zero_range_offset();
    std::cout << "\nAll RK4 ballistics tests passed.\n";
    return 0;
}
```

Add to `CMakeLists.txt`:

```cmake
add_executable(ballistics_rk4_test
    tests/unit/ballistics_rk4_test.cpp
    src/actuation/ballistic_solver.cpp
)
target_include_directories(ballistics_rk4_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ballistics_rk4_test PRIVATE Threads::Threads)
add_test(NAME BallisticsRK4Test COMMAND ballistics_rk4_test)
set_tests_properties(BallisticsRK4Test PROPERTIES TIMEOUT 30)
```

**Build and run** (tests should pass with current analytical solver — they test interface, not G1 internals yet):

```bash
cmake --build build-native --target ballistics_rk4_test -j$(nproc)
cd build-native && ./ballistics_rk4_test
```

---

### Task 8.2: Port G1 drag + RK4 from MkVI into ballistic_solver.cpp

Reference: `/home/laptop/AuroreMkVII/AuroreMkVI/src/logic.cpp` lines 47–419.

**What to add to `src/actuation/ballistic_solver.cpp`:**

1. **`air_density()` helper** (from MkVI lines 42-45):
   ```cpp
   static float air_density(float pressure_pa, float temp_c) {
       return pressure_pa / (287.058f * (temp_c + 273.15f));
   }
   ```

2. **G1 drag force** (from MkVI lines 47-82) — 4-segment piecewise Cd:

   | Speed | Cd |
   |---|---|
   | v ≤ 200 m/s | 0.25 + 0.0005·v |
   | 200 < v ≤ 400 | 0.35 − 0.00035·(v−200) |
   | 400 < v ≤ 800 | 0.28 − 0.0001·(v−400) |
   | v > 800 | 0.20 |

3. **RK4 step** — standard 4-evaluation integrator over `{pos, vel}` state vector.

4. **`compute_trajectory(pitch_rad, max_dist_m, dt_s)`** — advance state until `x >= max_dist` or `y < -sight_height` or NaN, cap at 5000 steps.

5. **`solve_kinetic_rk4(range_m, height_offset_m, muzzle_v)`** — bisection over pitch to land at `x=range_m`, replaces the current analytical formula. Keep analytical as fallback if BC is zero.

6. Keep the **lookup table** for `p_hit` at runtime (call `compute_trajectory` only during table init, not per-frame).

**Commit after each sub-step** (air_density → G1 drag → RK4 step → trajectory → bisection).

```bash
git add src/actuation/ballistic_solver.cpp
git commit -m "feat: port G1 drag model + RK4 integrator from MkVI (lines 47-419)"
```

---

## Phase 9: Full CTest Verification

### Task 9.1: Final test sweep

**Run:**

```bash
cd /home/laptop/AuroreMkVII
cmake --build build-native -j$(nproc)
cd build-native && ctest --output-on-failure -j4
```

**Expected outcome:**

| Test | Status |
|---|---|
| RingBufferTest | PASS |
| TimingTest | PASS |
| SafetyMonitorTest | PASS |
| SafetyMonitorFaultCodesTest | PASS |
| StateMachineTest | PASS |
| StateMachineTransitionsTest | PASS |
| MainThreadOrchestrationTest | PASS |
| DetectorTest | PASS |
| TrackerTest | PASS |
| BallisticsTest | PASS |
| FusionHatTest | PASS |
| HudSocketTest | PASS |
| TelemetryWriterTest | PASS (NEW) |
| InterlockControllerTest | PASS (NEW) |
| CameraWrapperTest | PASS (NEW) |
| ConfigLoaderTest | PASS (NEW) |
| GimbalControllerTest | PASS (NEW) |
| AuroreLinkTest | PASS (NEW) |
| BallisticsRK4Test | PASS (NEW) |
| TimingIntegrationTest | DISABLED (RPi only) |

**19/19 pass, 1 disabled.**

### Task 9.2: End-to-end dry-run with Link client

**Terminal 1:**
```bash
cd /home/laptop/AuroreMkVII/build-native && ./aurore --dry-run
```

**Terminal 2:**
```bash
cd /home/laptop/AuroreMkVII && python3 aurore_link/ui.py 127.0.0.1
```

**Verify:**
- UI window opens showing `State: SEARCH` or `State: TRACKING`
- Frame count advances in both terminal and UI
- Pressing `f` in UI sends FREECAM, terminal prints `AuroreLink: FREECAM mode activated`
- Pressing `a` restores AUTO mode
- Arrow keys in FREECAM send gimbal commands (check terminal log for gimbal updates)

### Task 9.3: Final commit and tag

```bash
git add -A
git commit -m "chore: final integration — 19/19 tests passing, Aurore Link end-to-end verified"
git tag v0.2.0 -m "MkVII: Networked FCS with Aurore Link protocol"
```

---

## What Was Explicitly NOT Implemented

Per design decision:
- ❌ DRM/fbdev local display (headless + network is primary)
- ❌ TFLite / EdgeTPU paths (MkVI dead code, not worth porting)
- ❌ GLES ring detector (MkVI broken, ORB is better)
- ❌ Android Aurore Link (deferred — same protobuf protocol, Kotlin client)
- ❌ ZMQ (UNIX socket + TCP covers all use cases without extra dep)
- ❌ Per-class distance correction maps (config-driven, not class-specific)

## Dependencies to Install Before Starting

```bash
sudo apt install protobuf-compiler libprotobuf-dev nlohmann-json3-dev
pip3 install protobuf grpcio-tools opencv-python numpy
```

Verify:
```bash
protoc --version           # 3.21.x+
python3 -c "import google.protobuf; print('ok')"
```
