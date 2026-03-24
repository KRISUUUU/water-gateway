#include "host_test_stubs.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_migration.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>

using namespace config_store;

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

int main() {
    printf("=== test_config_migration ===\n");
    test_current_version_passes_through();
    test_v0_migrates_to_current();
    test_v0_blank_gets_defaults();
    test_future_version_fails();
    test_v0_preserves_wifi();
    printf("All config migration tests passed.\n");
    return 0;
}
