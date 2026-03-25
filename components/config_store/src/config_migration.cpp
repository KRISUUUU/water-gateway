#include "config_store/config_migration.hpp"

namespace config_store {

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

    // Future migrations would go here:
    // if (current.version == 1) { current = migrate_v1_to_v2(current); }
    // if (current.version == 2) { current = migrate_v2_to_v3(current); }

    if (current.version != kCurrentConfigVersion) {
        return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
    }

    return common::Result<AppConfig>::ok(current);
}

} // namespace config_store
