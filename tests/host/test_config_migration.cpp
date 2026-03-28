#include "host_test_stubs.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_migration.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace config_store;

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

constexpr const char* kLegacyPasswordHash =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

AppConfigV1 make_v1_config_blob() {
    AppConfigV1 cfg{};
    cfg.version = 1;
    std::strncpy(cfg.device.name, "Legacy Gateway", sizeof(cfg.device.name) - 1);
    std::strncpy(cfg.device.hostname, "legacy-node", sizeof(cfg.device.hostname) - 1);
    std::strncpy(cfg.wifi.ssid, "LegacyWiFi", sizeof(cfg.wifi.ssid) - 1);
    std::strncpy(cfg.wifi.password, "legacy-secret", sizeof(cfg.wifi.password) - 1);
    cfg.wifi.max_retries = 7;
    cfg.mqtt.enabled = true;
    std::strncpy(cfg.mqtt.host, "broker.local", sizeof(cfg.mqtt.host) - 1);
    cfg.mqtt.port = 2883;
    std::strncpy(cfg.mqtt.username, "meter-user", sizeof(cfg.mqtt.username) - 1);
    std::strncpy(cfg.mqtt.password, "meter-pass", sizeof(cfg.mqtt.password) - 1);
    std::strncpy(cfg.mqtt.prefix, "legacy-prefix", sizeof(cfg.mqtt.prefix) - 1);
    std::strncpy(cfg.mqtt.client_id, "legacy-client", sizeof(cfg.mqtt.client_id) - 1);
    cfg.mqtt.qos = 1;
    cfg.mqtt.use_tls = true;
    cfg.radio.frequency_khz = 868300;
    cfg.radio.data_rate = 2;
    cfg.radio.auto_recovery = false;
    cfg.logging.level = 4;
    std::memcpy(cfg.auth.admin_password_hash, kLegacyPasswordHash, std::strlen(kLegacyPasswordHash));
    cfg.auth.admin_password_hash[std::strlen(kLegacyPasswordHash)] = '\0';
    cfg.auth.session_timeout_s = 7200;
    return cfg;
}

} // namespace

static void test_current_version_passes_through() {
    auto cfg = AppConfig::make_default();
    cfg.version = kCurrentConfigVersion;
    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    printf("  PASS: current version passes through\n");
}

static void test_v0_migrates_to_current() {
    AppConfig cfg{};
    cfg.version = 0;
    std::strncpy(cfg.device.name, "MyDevice", sizeof(cfg.device.name) - 1);
    std::strncpy(cfg.device.hostname, "myhost", sizeof(cfg.device.hostname) - 1);

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(std::strcmp(result.value().device.name, "MyDevice") == 0);
    assert(std::strcmp(result.value().device.hostname, "myhost") == 0);
    // New fields should have defaults
    assert(result.value().radio.frequency_khz == 868950);
    assert(result.value().auth.session_timeout_s == 3600);
    printf("  PASS: v0 migrates to current with field preservation\n");
}

static void test_v0_blank_gets_defaults() {
    AppConfig cfg{};
    cfg.version = 0;

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(std::strcmp(result.value().device.name, "WMBus Gateway") == 0);
    assert(result.value().mqtt.port == 1883);
    printf("  PASS: v0 blank config gets all defaults\n");
}

static void test_future_version_fails() {
    AppConfig cfg{};
    cfg.version = 999;

    auto result = migrate_to_current(cfg);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::ConfigVersionMismatch);
    printf("  PASS: future version rejected\n");
}

static void test_v0_preserves_wifi() {
    AppConfig cfg{};
    cfg.version = 0;
    std::strncpy(cfg.wifi.ssid, "TestNetwork", sizeof(cfg.wifi.ssid) - 1);
    std::strncpy(cfg.wifi.password, "secret123", sizeof(cfg.wifi.password) - 1);
    cfg.wifi.max_retries = 5;

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(std::strcmp(result.value().wifi.ssid, "TestNetwork") == 0);
    assert(std::strcmp(result.value().wifi.password, "secret123") == 0);
    assert(result.value().wifi.max_retries == 5);
    printf("  PASS: v0 preserves WiFi credentials\n");
}

static void test_v1_blob_migrates_to_current_and_preserves_hash() {
    assert(sizeof(AppConfigV1) == config_v1_blob_size());

    const AppConfigV1 legacy = make_v1_config_blob();
    std::array<uint8_t, sizeof(AppConfigV1)> blob{};
    std::memcpy(blob.data(), &legacy, sizeof(legacy));

    auto result = migrate_v1_blob(blob.data(), blob.size());
    assert(result.is_ok());

    const auto& migrated = result.value();
    assert(migrated.version == kCurrentConfigVersion);
    assert(std::strcmp(migrated.device.name, "Legacy Gateway") == 0);
    assert(std::strcmp(migrated.device.hostname, "legacy-node") == 0);
    assert(std::strcmp(migrated.wifi.ssid, "LegacyWiFi") == 0);
    assert(std::strcmp(migrated.mqtt.host, "broker.local") == 0);
    assert(std::strcmp(migrated.auth.admin_password_hash, kLegacyPasswordHash) == 0);
    assert(migrated.auth.session_timeout_s == 7200);
    printf("  PASS: v1 blob migrates to current and preserves password hash\n");
}

static void test_v1_blob_invalid_sizes_fail_safely() {
    const AppConfigV1 legacy = make_v1_config_blob();
    std::array<uint8_t, sizeof(AppConfigV1)> blob{};
    std::memcpy(blob.data(), &legacy, sizeof(legacy));

    auto short_blob = migrate_v1_blob(blob.data(), blob.size() - 1);
    assert(short_blob.is_error());
    assert(short_blob.error() == common::ErrorCode::ConfigMigrationFailed);

    auto long_blob = migrate_v1_blob(blob.data(), blob.size() + 1);
    assert(long_blob.is_error());
    assert(long_blob.error() == common::ErrorCode::ConfigMigrationFailed);

    auto null_blob = migrate_v1_blob(nullptr, blob.size());
    assert(null_blob.is_error());
    assert(null_blob.error() == common::ErrorCode::ConfigMigrationFailed);
    printf("  PASS: invalid v1 blob sizes fail safely\n");
}

int main() {
    printf("=== test_config_migration ===\n");
    test_current_version_passes_through();
    test_v0_migrates_to_current();
    test_v0_blank_gets_defaults();
    test_future_version_fails();
    test_v0_preserves_wifi();
    test_v1_blob_migrates_to_current_and_preserves_hash();
    test_v1_blob_invalid_sizes_fail_safely();
    printf("All config migration tests passed.\n");
    return 0;
}
