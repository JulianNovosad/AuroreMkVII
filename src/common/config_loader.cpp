#include "aurore/config_loader.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <vector>

namespace aurore {

struct ConfigLoader::Impl {
    nlohmann::json data;

    static std::vector<std::string> split_key(const std::string& key) {
        std::vector<std::string> parts;
        std::stringstream ss(key);
        std::string part;
        while (std::getline(ss, part, '.')) {
            parts.push_back(part);
        }
        return parts;
    }

    const nlohmann::json* find_value(const std::string& key) const {
        auto parts = split_key(key);
        const nlohmann::json* current = &data;
        for (const auto& part : parts) {
            if (!current->is_object() || !current->contains(part)) {
                return nullptr;
            }
            current = &current->at(part);
        }
        return current;
    }
};

ConfigLoader::ConfigLoader(const std::string& path) : impl_(std::make_shared<Impl>()) {
    if (!path.empty()) {
        load(path);
    }
}

bool ConfigLoader::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        loaded_ = false;
        return false;
    }

    try {
        impl_->data = nlohmann::json::parse(file);
        loaded_ = true;
        return true;
    } catch (const nlohmann::json::parse_error&) {
        loaded_ = false;
        return false;
    }
}

int ConfigLoader::get_int(const std::string& key, int default_value) const {
    if (!loaded_) {
        return default_value;
    }
    const auto* value = impl_->find_value(key);
    if (value && value->is_number_integer()) {
        return value->get<int>();
    }
    return default_value;
}

float ConfigLoader::get_float(const std::string& key, float default_value) const {
    if (!loaded_) {
        return default_value;
    }
    const auto* value = impl_->find_value(key);
    if (value && value->is_number()) {
        return value->get<float>();
    }
    return default_value;
}

bool ConfigLoader::get_bool(const std::string& key, bool default_value) const {
    if (!loaded_) {
        return default_value;
    }
    const auto* value = impl_->find_value(key);
    if (value && value->is_boolean()) {
        return value->get<bool>();
    }
    return default_value;
}

std::string ConfigLoader::get_string(const std::string& key, const std::string& default_value) const {
    if (!loaded_) {
        return default_value;
    }
    const auto* value = impl_->find_value(key);
    if (value && value->is_string()) {
        return value->get<std::string>();
    }
    return default_value;
}

}  // namespace aurore
