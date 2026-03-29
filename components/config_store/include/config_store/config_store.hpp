#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"

namespace config_store {

enum class ConfigLoadSource : uint8_t {
    None = 0,
    PrimaryNvs,
    BackupNvs,
    Defaults,
};

struct ConfigRuntimeStatus {
    bool used_defaults = false;
    bool defaults_persisted = false;
    bool defaults_persist_deferred = false;
    bool loaded_from_backup = false;
    ConfigLoadSource load_source = ConfigLoadSource::None;
    common::ErrorCode last_load_error = common::ErrorCode::OK;
    common::ErrorCode last_persist_error = common::ErrorCode::OK;
    common::ErrorCode last_migration_error = common::ErrorCode::OK;
    uint32_t initialize_count = 0;
    uint32_t load_attempts = 0;
    uint32_t load_failures = 0;
    uint32_t primary_read_failures = 0;
    uint32_t backup_read_failures = 0;
    uint32_t validation_failures = 0;
    uint32_t migration_attempts = 0;
    uint32_t migration_failures = 0;
    uint32_t save_attempts = 0;
    uint32_t save_successes = 0;
    uint32_t save_failures = 0;
    uint32_t save_validation_rejects = 0;
};

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

    // Fast boot-path helper to avoid full AppConfig copies.
    bool wifi_is_configured() const;

    // Validates and persists the given config. Returns validation errors if invalid.
    common::Result<ValidationResult> save(const AppConfig& new_config);

    // Resets config to factory defaults and persists.
    common::Result<void> reset_to_defaults();

    bool is_initialized() const {
        return initialized_;
    }
    bool is_loaded() const {
        return loaded_;
    }
    ConfigRuntimeStatus runtime_status() const;

  private:
    ConfigStore() = default;

    common::Result<void> load_from_nvs();
    common::Result<void> persist_to_nvs(const AppConfig& cfg);

    bool initialized_ = false;
    bool loaded_ = false;
    AppConfig config_{};
    ConfigRuntimeStatus runtime_status_{};

#ifndef HOST_TEST_BUILD
    void* mutex_ = nullptr;
#endif
};

} // namespace config_store
