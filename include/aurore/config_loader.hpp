#pragma once
#include <memory>
#include <string>

namespace aurore {

class ConfigLoader {
public:
    explicit ConfigLoader(const std::string& path = "");
    bool load(const std::string& path);
    int get_int(const std::string& key, int default_value = 0) const;
    float get_float(const std::string& key, float default_value = 0.f) const;
    bool get_bool(const std::string& key, bool default_value = false) const;
    std::string get_string(const std::string& key, const std::string& default_value = "") const;
    bool is_loaded() const { return loaded_; }

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    bool loaded_{false};
};

}  // namespace aurore
