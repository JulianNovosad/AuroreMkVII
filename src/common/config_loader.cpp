/**
 * @file config_loader.cpp
 * @brief JSON configuration loader using nlohmann/json
 *
 * Loads system configuration from config.json with graceful fallback
 * to default values. Supports nested keys via dot notation.
 */

#include "aurore/config_loader.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace aurore {

/**
 * @brief Internal implementation (pimpl pattern)
 */
struct ConfigLoader::Impl {
    nlohmann::json config_data;
    std::string config_path;
    bool loaded = false;

    /**
     * @brief Get nested JSON value by dot-notation path
     *
     * @param path Dot-separated path (e.g., "system.frame_rate_hz")
     * @return Pointer to value if found, nullptr otherwise
     */
    const nlohmann::json* get_value(const std::string& path) const {
        if (!loaded) {
            return nullptr;
        }

        std::istringstream iss(path);
        std::string key;
        const nlohmann::json* current = &config_data;

        while (std::getline(iss, key, '.')) {
            if (!current->contains(key)) {
                return nullptr;
            }
            current = &(*current)[key];
        }

        return current;
    }

    bool load_from_file(const std::string& path) {
        config_path = path;

        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "ConfigLoader: Cannot open config file: " << path << std::endl;
            return false;
        }

        try {
            file >> config_data;
            loaded = true;
            std::cout << "ConfigLoader: Loaded " << path << std::endl;
            return true;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "ConfigLoader: JSON parse error: " << e.what() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "ConfigLoader: Error loading config: " << e.what() << std::endl;
            return false;
        }
    }
};

ConfigLoader::ConfigLoader(const std::string& path) : impl_(std::make_shared<Impl>()) {
    if (!path.empty()) {
        load(path);
    }
}

bool ConfigLoader::load(const std::string& path) {
    if (impl_) {
        bool result = impl_->load_from_file(path);
        loaded_ = impl_->loaded;
        return result;
    }
    return false;
}

int ConfigLoader::get_int(const std::string& key, int default_value) const {
    if (!impl_ || !impl_->loaded) {
        return default_value;
    }

    const nlohmann::json* value = impl_->get_value(key);
    if (value == nullptr) {
        return default_value;
    }

    try {
        return value->get<int>();
    } catch (const nlohmann::json::type_error& e) {
        std::cerr << "ConfigLoader: Type error for key '" << key << "': " << e.what() << std::endl;
    }

    return default_value;
}

float ConfigLoader::get_float(const std::string& key, float default_value) const {
    if (!impl_ || !impl_->loaded) {
        return default_value;
    }

    const nlohmann::json* value = impl_->get_value(key);
    if (value == nullptr) {
        return default_value;
    }

    try {
        return value->get<float>();
    } catch (const nlohmann::json::type_error& e) {
        std::cerr << "ConfigLoader: Type error for key '" << key << "': " << e.what() << std::endl;
    }

    return default_value;
}

bool ConfigLoader::get_bool(const std::string& key, bool default_value) const {
    if (!impl_ || !impl_->loaded) {
        return default_value;
    }

    const nlohmann::json* value = impl_->get_value(key);
    if (value == nullptr) {
        return default_value;
    }

    try {
        return value->get<bool>();
    } catch (const nlohmann::json::type_error& e) {
        std::cerr << "ConfigLoader: Type error for key '" << key << "': " << e.what() << std::endl;
    }

    return default_value;
}

std::string ConfigLoader::get_string(const std::string& key,
                                     const std::string& default_value) const {
    if (!impl_ || !impl_->loaded) {
        return default_value;
    }

    const nlohmann::json* value = impl_->get_value(key);
    if (value == nullptr) {
        return default_value;
    }

    try {
        return value->get<std::string>();
    } catch (const nlohmann::json::type_error& e) {
        std::cerr << "ConfigLoader: Type error for key '" << key << "': " << e.what() << std::endl;
    }

    return default_value;
}

const nlohmann::json& ConfigLoader::get_json() const {
    static const nlohmann::json empty_json = nlohmann::json::object();
    if (!impl_ || !impl_->loaded) {
        return empty_json;
    }
    return impl_->config_data;
}

}  // namespace aurore
