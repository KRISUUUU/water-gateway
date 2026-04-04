#include "config_store/config_migration.hpp"
#include "protocol_driver/protocol_ids.hpp"

namespace config_store {

namespace {
} // namespace

// Version 0 represents an unversioned (fresh/blank) config blob.
// Migration from v0 to v1 applies all default values.
static common::Result<AppConfig> migrate_v0_to_v1(const AppConfig& old) {
    AppConfig migrated = AppConfig::make_default();

    // Preserve any fields that were already set in v0
    // (v0 had device.name, device.hostname, wifi, mqtt basics)
    if (old.device.name[0] != '\0') {
        std::memcpy(migrated.device.name, old.device.name, sizeof(migrated.device.name));
    }
    if (old.device.hostname[0] != '\0') {
        std::memcpy(migrated.device.hostname, old.device.hostname,
                    sizeof(migrated.device.hostname));
    }
    if (old.wifi.ssid[0] != '\0') {
        std::memcpy(&migrated.wifi, &old.wifi, sizeof(migrated.wifi));
    }
    if (old.mqtt.host[0] != '\0') {
        std::memcpy(&migrated.mqtt, &old.mqtt, sizeof(migrated.mqtt));
    }

    migrated.version = 1;
    return common::Result<AppConfig>::ok(migrated);
}

static common::Result<AppConfig> migrate_v1_to_v2(const AppConfig& old) {
    AppConfig migrated = old;
    migrated.version = 2;
    migrated.auth.admin_password_hash[sizeof(migrated.auth.admin_password_hash) - 1] = '\0';
    return common::Result<AppConfig>::ok(migrated);
}

static common::Result<AppConfig> migrate_v2_to_v3(const AppConfig& old) {
    AppConfig migrated = old;
    migrated.version = 3;
    // New in v3: scheduler mode and enabled profiles. Existing configs default
    // to Locked mode with only T-mode enabled — preserving current behaviour.
    migrated.radio.scheduler_mode   = protocol_driver::RadioSchedulerMode::Locked;
    migrated.radio.enabled_profiles = protocol_driver::kRadioProfileMaskWMbusT868;
    return common::Result<AppConfig>::ok(migrated);
}

static common::Result<AppConfig> migrate_v3_to_v4(const AppConfig& old) {
    AppConfig migrated = old;
    migrated.version = 4;
    // New in v4: PRIOS capture campaign fields. Default to disabled so
    // existing deployments continue operating in T-mode without any change.
    migrated.radio.prios_capture_campaign  = false;
    migrated.radio.prios_manchester_enabled = false;
    return common::Result<AppConfig>::ok(migrated);
}

common::Result<AppConfig> migrate_to_current(const AppConfig& old_config) {
    if (old_config.version == kCurrentConfigVersion) {
        return common::Result<AppConfig>::ok(old_config);
    }

    if (old_config.version > kCurrentConfigVersion) {
        return common::Result<AppConfig>::error(common::ErrorCode::ConfigVersionMismatch);
    }

    AppConfig current = old_config;

    // Chain migrations sequentially
    if (current.version == 0) {
        auto result = migrate_v0_to_v1(current);
        if (result.is_error()) {
            return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
        }
        current = result.value();
    }

    if (current.version == 1) {
        auto result = migrate_v1_to_v2(current);
        if (result.is_error()) {
            return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
        }
        current = result.value();
    }

    if (current.version == 2) {
        auto result = migrate_v2_to_v3(current);
        if (result.is_error()) {
            return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
        }
        current = result.value();
    }

    if (current.version == 3) {
        auto result = migrate_v3_to_v4(current);
        if (result.is_error()) {
            return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
        }
        current = result.value();
    }

    if (current.version != kCurrentConfigVersion) {
        return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
    }

    return common::Result<AppConfig>::ok(current);
}

} // namespace config_store
