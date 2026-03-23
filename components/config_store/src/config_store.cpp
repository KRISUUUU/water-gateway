#include "config_store/config_store.hpp"

#include "config_store/config_migration.hpp"

namespace config_store {

ConfigStore& ConfigStore::instance() {
    static ConfigStore store;
    return store;
}

common::Result<void> ConfigStore::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    current_config_ = AppConfig{};
    const auto validation = validate_config(current_config_);
    has_valid_config_ = validation.valid;
    initialized_ = true;

    return common::Result<void>();
}

common::Result<AppConfig> ConfigStore::load() const {
    if (!initialized_) {
        return common::Result<AppConfig>(common::ErrorCode::NotInitialized);
    }

    return common::Result<AppConfig>(current_config_);
}

common::Result<void> ConfigStore::save(const AppConfig& config) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    const auto migrated = migrate_to_current(config);
    if (!migrated.ok()) {
        return common::Result<void>(migrated.error());
    }

    const auto validation = validate_config(migrated.value());
    if (!validation.valid) {
        return common::Result<void>(common::ErrorCode::ValidationFailed);
    }

    current_config_ = migrated.value();
    has_valid_config_ = true;

    return common::Result<void>();
}

common::Result<void> ConfigStore::reset_to_defaults() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    current_config_ = AppConfig{};
    has_valid_config_ = validate_config(current_config_).valid;

    return common::Result<void>();
}

bool ConfigStore::is_initialized() const {
    return initialized_;
}

bool ConfigStore::has_valid_config() const {
    return has_valid_config_;
}

}  // namespace config_store
