#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"

namespace config_store {

// Singleton configuration store backed by NVS.
//
// Lifecycle:
//   1. initialize() — opens NVS, loads config (or applies defaults)
//   2. config() — access current config (read-only)
//   3. save(new_config) — validate + migrate + persist
//   4. reset_to_defaults() — write defaults, caller should reboot
//
// Thread safety: the config is read from multiple tasks but written only
// from the HTTP/API task. A mutex protects the config_ member.
class ConfigStore {
public:
    static ConfigStore& instance();

    // Opens NVS namespace and loads config. If no config exists, writes defaults.
    // If stored config is an older version, migrates to current.
    common::Result<void> initialize();

    // Returns a copy of the current config. Safe to call from any task.
    AppConfig config() const;

    // Validates and persists the given config. Returns validation errors if invalid.
    common::Result<ValidationResult> save(const AppConfig& new_config);

    // Resets config to factory defaults and persists.
    common::Result<void> reset_to_defaults();

    bool is_initialized() const { return initialized_; }
    bool is_loaded() const { return loaded_; }

private:
    ConfigStore() = default;

    common::Result<void> load_from_nvs();
    common::Result<void> persist_to_nvs(const AppConfig& cfg);

    bool initialized_ = false;
    bool loaded_ = false;
    AppConfig config_{};

#ifndef HOST_TEST_BUILD
    void* mutex_ = nullptr;
#endif
};

} // namespace config_store
