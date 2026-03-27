#include "config_store/config_migration.hpp"
#include <cstring>

namespace config_store {

// ---- V1 struct layout -------------------------------------------------------
// Must exactly mirror the AppConfig layout from config version 1.
// The ONLY difference from V2 is admin_password_hash[98] instead of [128].
// All other fields and their positions are identical.

namespace {

struct AuthConfigV1 {
    char admin_password_hash[98];
    uint32_t session_timeout_s;
};

struct AppConfigV1 {
    uint32_t version;
    DeviceConfig device;
    WifiConfig wifi;
    MqttConfig mqtt;
    RadioConfig radio;
    LoggingConfig logging;
    AuthConfigV1 auth;
};

} // namespace

size_t config_v1_blob_size() {
    return sizeof(AppConfigV1);
}

common::Result<AppConfig> migrate_v1_blob(const uint8_t* blob, size_t blob_size) {
    if (!blob || blob_size != sizeof(AppConfigV1)) {
        return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
    }

    AppConfigV1 v1{};
    std::memcpy(&v1, blob, sizeof(AppConfigV1));

    AppConfig v2 = AppConfig::make_default();
    v2.version = kCurrentConfigVersion;

    // Copy fields that are layout-identical in V1 and V2.
    std::memcpy(&v2.device, &v1.device, sizeof(v2.device));
    std::memcpy(&v2.wifi, &v1.wifi, sizeof(v2.wifi));
    std::memcpy(&v2.mqtt, &v1.mqtt, sizeof(v2.mqtt));
    std::memcpy(&v2.radio, &v1.radio, sizeof(v2.radio));
    std::memcpy(&v2.logging, &v1.logging, sizeof(v2.logging));

    // auth.admin_password_hash is intentionally NOT copied: the field grew from
    // [98] to [128] and the new format (PBKDF2) is incompatible with the old hash
    // format stored in V1. The user must set a new password after upgrade.
    // (SHA-256 hashes stored in V1 can still be verified by the auth service
    // via legacy-format detection, but they are not migrated here to avoid
    // silently preserving weaker hashes across the security boundary.)
    std::memset(v2.auth.admin_password_hash, 0, sizeof(v2.auth.admin_password_hash));
    v2.auth.session_timeout_s = v1.auth.session_timeout_s;

    return common::Result<AppConfig>::ok(v2);
}

// Version 0 → Version 1: apply all defaults, preserve existing fields.
static common::Result<AppConfig> migrate_v0_to_v1(const AppConfig& old) {
    AppConfig migrated = AppConfig::make_default();

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

// Version 1 → Version 2: admin_password_hash field grew from [98] to [128].
// The in-memory V1 blob is handled separately via migrate_v1_blob() (raw bytes).
// This in-struct path is used when a V1 AppConfig is already in RAM (e.g. test).
static common::Result<AppConfig> migrate_v1_to_v2(const AppConfig& old) {
    AppConfig migrated = old;
    migrated.version = 2;
    // admin_password_hash content is preserved as-is; the field is now 128 bytes
    // but old hashes (<= 97 chars) still fit and are verified by the legacy path.
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

    if (current.version != kCurrentConfigVersion) {
        return common::Result<AppConfig>::error(common::ErrorCode::ConfigMigrationFailed);
    }

    return common::Result<AppConfig>::ok(current);
}

} // namespace config_store
