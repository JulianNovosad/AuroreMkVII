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
    cfg.video_port     = 19001;
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
    cfg.video_port     = 19011;
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
    [[maybe_unused]] ssize_t n = ::recv(client, &net_len, 4, MSG_DONTWAIT);
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
    cfg.video_port     = 19021;
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
    cmd.mutable_mode_switch()->set_mode(::aurore::OperatingMode::FREECAM_MODE);
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

void test_freecam_callback_fires_on_command() {
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19030;
    cfg.video_port     = 19031;
    cfg.command_port   = 19032;
    AuroreLinkServer server(cfg);

    float received_az = 0.f, received_el = 0.f, received_vel = 0.f;
    server.set_freecam_callback([&](float az, float el, float vel) {
        received_az = az;
        received_el = el;
        received_vel = vel;
    });
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int cmd_client = connect_to(19032);
    assert(cmd_client >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    aurore::FreecamTarget cmd;
    cmd.set_az_deg(45.5f);
    cmd.set_el_deg(-10.0f);
    cmd.set_velocity_dps(30.0f);
    
    aurore::Command wrapper;
    wrapper.mutable_freecam()->CopyFrom(cmd);
    
    std::string data;
    wrapper.SerializeToString(&data);
    uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));
    ::send(cmd_client, &net_len, 4, 0);
    ::send(cmd_client, data.data(), data.size(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    assert(std::abs(received_az - 45.5f) < 0.1f);
    assert(std::abs(received_el - (-10.0f)) < 0.1f);
    assert(std::abs(received_vel - 30.0f) < 0.1f);

    ::close(cmd_client);
    server.stop();
    std::cout << "PASS: freecam callback fires with correct target data\n";
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    test_server_starts_and_stops();
    test_telemetry_client_receives_broadcast();
    test_mode_callback_fires_on_command();
    test_freecam_callback_fires_on_command();
    google::protobuf::ShutdownProtobufLibrary();
    std::cout << "\nAll AuroreLink tests passed.\n";
    return 0;
}
