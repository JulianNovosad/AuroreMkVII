#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>

#include "aurore/fusion_hat.hpp"

namespace fs = std::filesystem;

namespace {

// Test counters
std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)
#define ASSERT_NEAR(a, b, tol) do { \
    if (std::abs((a) - (b)) > (tol)) \
        throw std::runtime_error("Assertion failed: " #a " not near " #b); \
} while(0)

void setup_mock_sysfs(const std::string& base) {
    fs::create_directories(base);
    std::ofstream(base + "/version") << "1.0.0";
    std::ofstream(base + "/firmware_version") << "0.9.0";
    for (int i = 0; i < 12; ++i) {
        std::string p = base + "/pwm" + std::to_string(i);
        fs::create_directories(p);
        std::ofstream(p + "/enable") << "0";
        std::ofstream(p + "/period") << "0";
        std::ofstream(p + "/duty_cycle") << "0";
    }
}

void setup_mock_proc(const std::string& base) {
    fs::create_directories(base + "/hat0");
    std::ofstream(base + "/hat0/uuid") << "9daeea78-0000-0774-000a-582369ac3e02";
}

}  // anonymous namespace

using namespace aurore;

// 51. Sysfs Mock Fail
TEST(test_sysfs_mock_fail) {
    std::string mock_sys = "/tmp/fusion_hat_mock_sys_51";
    std::string mock_proc = "/tmp/fusion_hat_mock_proc_51";
    fs::remove_all(mock_sys); fs::remove_all(mock_proc);
    setup_mock_sysfs(mock_sys); setup_mock_proc(mock_proc);
    FusionHatConfig config; config.max_i2c_retries = 0;
    FusionHat hat(config);
    hat.set_sysfs_base_for_test(mock_sys); hat.set_proc_base_for_test(mock_proc);
    ASSERT_TRUE(hat.init());
    fs::permissions(mock_sys + "/pwm0/duty_cycle", fs::perms::none);
    hat.set_servo_angle(0, 45.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_GT(hat.get_error_count(), 0);
}

// 52. Retry Exhaustion
TEST(test_retry_exhaustion) {
    std::string mock_sys = "/tmp/fusion_hat_mock_sys_52";
    std::string mock_proc = "/tmp/fusion_hat_mock_proc_52";
    fs::remove_all(mock_sys); fs::remove_all(mock_proc);
    setup_mock_sysfs(mock_sys); setup_mock_proc(mock_proc);
    FusionHatConfig config; config.max_i2c_retries = 2;
    FusionHat hat(config);
    hat.set_sysfs_base_for_test(mock_sys); hat.set_proc_base_for_test(mock_proc);
    ASSERT_TRUE(hat.init());
    fs::permissions(mock_sys + "/pwm1/duty_cycle", fs::perms::none);
    uint64_t start_errors = hat.get_error_count();
    hat.set_servo_angle(1, 45.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_GT(hat.get_error_count(), start_errors);
}

// 53. Endstop Enforcement
TEST(test_endstop_enforcement) {
    std::string mock_sys = "/tmp/fusion_hat_mock_sys_53";
    std::string mock_proc = "/tmp/fusion_hat_mock_proc_53";
    fs::remove_all(mock_sys); fs::remove_all(mock_proc);
    setup_mock_sysfs(mock_sys); setup_mock_proc(mock_proc);
    FusionHatConfig config; config.min_angle_deg = 10.0f; config.max_angle_deg = 170.0f;
    FusionHat hat(config);
    hat.set_sysfs_base_for_test(mock_sys); hat.set_proc_base_for_test(mock_proc);
    ASSERT_TRUE(hat.init());
    hat.set_servo_angle(0, 5.0f);
    ASSERT_NEAR(hat.get_servo_status(0).angle_deg, 10.0f, 0.1f);
    hat.set_servo_angle(0, 180.0f);
    ASSERT_NEAR(hat.get_servo_status(0).angle_deg, 170.0f, 0.1f);
}

// 54. Rate Limit Clamp
TEST(test_rate_limit_clamp_reimplemented) {
    std::string mock_sys = "/tmp/fusion_hat_mock_sys_54";
    std::string mock_proc = "/tmp/fusion_hat_mock_proc_54";
    fs::remove_all(mock_sys); fs::remove_all(mock_proc);
    setup_mock_sysfs(mock_sys); setup_mock_proc(mock_proc);
    
    FusionHatConfig config;
    config.enable_rate_limit = true;
    config.max_angular_velocity_dps = 100.0f;
    FusionHat hat(config);
    hat.set_sysfs_base_for_test(mock_sys);
    hat.set_proc_base_for_test(mock_proc);
    ASSERT_TRUE(hat.init());
    
    // Move to 90 first
    hat.set_servo_angle(0, 90.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Ensure it got to 90
    
    float start_angle = hat.get_servo_status(0).angle_deg;
    std::cout << "    Start angle: " << start_angle << std::endl;
    
    hat.set_servo_angle(0, 180.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Wait 200ms -> should move ~20 degrees
    
    float end_angle = hat.get_servo_status(0).angle_deg;
    std::cout << "    End angle: " << end_angle << std::endl;
    
    // It should be around 110. Let's be lenient but ensure it moved from 90.
    ASSERT_GT(end_angle, 100.0f);
    ASSERT_GT(125.0f, end_angle);
}

// 55. Async Queue Reordering
TEST(test_async_reordering) {
    std::string mock_sys = "/tmp/fusion_hat_mock_sys_55";
    std::string mock_proc = "/tmp/fusion_hat_mock_proc_55";
    fs::remove_all(mock_sys); fs::remove_all(mock_proc);
    setup_mock_sysfs(mock_sys); setup_mock_proc(mock_proc);
    FusionHat hat;
    hat.set_sysfs_base_for_test(mock_sys); hat.set_proc_base_for_test(mock_proc);
    ASSERT_TRUE(hat.init());
    fs::permissions(mock_sys + "/pwm2/duty_cycle", fs::perms::none);
    hat.set_servo_angle(2, 45.0f);
    hat.set_servo_angle(2, 50.0f);
    hat.set_servo_angle(2, 55.0f);
    hat.set_servo_enabled(2, false);
    fs::permissions(mock_sys + "/pwm2/duty_cycle", fs::perms::owner_write | fs::perms::owner_read);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_FALSE(hat.is_servo_enabled(2));
}

int main() {
    std::cout << "Running FusionHat Stress tests (v3)..." << std::endl;
    RUN_TEST(test_sysfs_mock_fail);
    RUN_TEST(test_retry_exhaustion);
    RUN_TEST(test_endstop_enforcement);
    RUN_TEST(test_rate_limit_clamp_reimplemented);
    RUN_TEST(test_async_reordering);
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
