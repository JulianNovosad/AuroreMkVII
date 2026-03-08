#include "aurore/config_loader.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace {

const std::string kTestJson = R"({
  "system": {"frame_rate_hz": 120, "use_preempt_rt": false},
  "gimbal": {"azimuth": {"velocity_limit_dps": 60.0}}
})";

std::string create_temp_config_file() {
    char temp_path[] = "/tmp/aurore_config_XXXXXX";
    int fd = mkstemp(temp_path);
    if (fd == -1) {
        return "";
    }
    std::string path = std::string(temp_path) + ".json";
    close(fd);
    
    std::ofstream out(path);
    if (!out.is_open()) {
        std::remove(temp_path);
        return "";
    }
    out << kTestJson;
    return path;
}

void remove_temp_file(const std::string& path) {
    if (!path.empty()) {
        std::remove(path.c_str());
    }
}

int test_load_returns_true_for_valid_file() {
    std::string path = create_temp_config_file();
    if (path.empty()) {
        std::cerr << "FAIL: test_load_returns_true_for_valid_file - could not create temp file\n";
        return 1;
    }

    aurore::ConfigLoader loader;
    bool result = loader.load(path);
    remove_temp_file(path);

    if (!result) {
        std::cerr << "FAIL: test_load_returns_true_for_valid_file - load returned false\n";
        return 1;
    }
    if (!loader.is_loaded()) {
        std::cerr << "FAIL: test_load_returns_true_for_valid_file - is_loaded returned false\n";
        return 1;
    }
    std::cout << "PASS: test_load_returns_true_for_valid_file\n";
    return 0;
}

int test_load_returns_false_for_missing_file() {
    aurore::ConfigLoader loader;
    bool result = loader.load("/nonexistent/path/to/config.json");

    if (result) {
        std::cerr << "FAIL: test_load_returns_false_for_missing_file - load returned true\n";
        return 1;
    }
    if (loader.is_loaded()) {
        std::cerr << "FAIL: test_load_returns_false_for_missing_file - is_loaded returned true\n";
        return 1;
    }
    std::cout << "PASS: test_load_returns_false_for_missing_file\n";
    return 0;
}

int test_get_int_reads_value() {
    std::string path = create_temp_config_file();
    if (path.empty()) {
        std::cerr << "FAIL: test_get_int_reads_value - could not create temp file\n";
        return 1;
    }

    aurore::ConfigLoader loader(path);
    remove_temp_file(path);

    int value = loader.get_int("system.frame_rate_hz", 0);
    if (value != 120) {
        std::cerr << "FAIL: test_get_int_reads_value - expected 120, got " << value << "\n";
        return 1;
    }
    std::cout << "PASS: test_get_int_reads_value\n";
    return 0;
}

int test_get_float_reads_value() {
    std::string path = create_temp_config_file();
    if (path.empty()) {
        std::cerr << "FAIL: test_get_float_reads_value - could not create temp file\n";
        return 1;
    }

    aurore::ConfigLoader loader(path);
    remove_temp_file(path);

    float value = loader.get_float("gimbal.azimuth.velocity_limit_dps", 0.f);
    if (value < 59.9f || value > 60.1f) {
        std::cerr << "FAIL: test_get_float_reads_value - expected ~60.0, got " << value << "\n";
        return 1;
    }
    std::cout << "PASS: test_get_float_reads_value\n";
    return 0;
}

int test_get_bool_reads_value() {
    std::string path = create_temp_config_file();
    if (path.empty()) {
        std::cerr << "FAIL: test_get_bool_reads_value - could not create temp file\n";
        return 1;
    }

    aurore::ConfigLoader loader(path);
    remove_temp_file(path);

    bool value = loader.get_bool("system.use_preempt_rt", true);
    if (value) {
        std::cerr << "FAIL: test_get_bool_reads_value - expected false, got true\n";
        return 1;
    }
    std::cout << "PASS: test_get_bool_reads_value\n";
    return 0;
}

int test_missing_key_returns_default() {
    std::string path = create_temp_config_file();
    if (path.empty()) {
        std::cerr << "FAIL: test_missing_key_returns_default - could not create temp file\n";
        return 1;
    }

    aurore::ConfigLoader loader(path);
    remove_temp_file(path);

    int int_default = loader.get_int("nonexistent.key", 42);
    if (int_default != 42) {
        std::cerr << "FAIL: test_missing_key_returns_default - int default expected 42, got " << int_default << "\n";
        return 1;
    }

    float float_default = loader.get_float("nonexistent.key", 3.14f);
    if (float_default < 3.13f || float_default > 3.15f) {
        std::cerr << "FAIL: test_missing_key_returns_default - float default expected 3.14, got " << float_default << "\n";
        return 1;
    }

    bool bool_default = loader.get_bool("nonexistent.key", true);
    if (!bool_default) {
        std::cerr << "FAIL: test_missing_key_returns_default - bool default expected true, got false\n";
        return 1;
    }

    std::string string_default = loader.get_string("nonexistent.key", "default_value");
    if (string_default != "default_value") {
        std::cerr << "FAIL: test_missing_key_returns_default - string default expected 'default_value', got '" << string_default << "'\n";
        return 1;
    }

    std::cout << "PASS: test_missing_key_returns_default\n";
    return 0;
}

}  // namespace

int main() {
    int failures = 0;

    failures += test_load_returns_true_for_valid_file();
    failures += test_load_returns_false_for_missing_file();
    failures += test_get_int_reads_value();
    failures += test_get_float_reads_value();
    failures += test_get_bool_reads_value();
    failures += test_missing_key_returns_default();

    if (failures == 0) {
        std::cout << "\nAll 6 tests passed.\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) failed.\n";
        return 1;
    }
}
