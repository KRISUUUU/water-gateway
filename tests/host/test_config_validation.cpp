#define HOST_TEST_BUILD
#include "host_test_stubs.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"
#include <cassert>
#include <cstdio>

using namespace config_store;

static void test_default_config_is_valid() {
    auto cfg = AppConfig::make_default();
    auto result = validate_config(cfg);
    // Default config has empty MQTT host when MQTT is enabled -> should fail
    assert(!result.valid);
    printf("  PASS: default config fails validation (MQTT host empty)\n");
}

static void test_valid_complete_config() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "192.168.1.10", sizeof(cfg.mqtt.host) - 1);
    auto result = validate_config(cfg);
    assert(result.valid);
    printf("  PASS: complete config passes validation\n");
}

static void test_empty_device_name_fails() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
    cfg.device.name[0] = '\0';
    auto result = validate_config(cfg);
    assert(!result.valid);
    bool found = false;
    for (auto& issue : result.issues) {
        if (issue.field == "device.name") found = true;
    }
    assert(found);
    printf("  PASS: empty device name fails\n");
}

static void test_empty_hostname_fails() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
    cfg.device.hostname[0] = '\0';
    auto result = validate_config(cfg);
    assert(!result.valid);
    printf("  PASS: empty hostname fails\n");
}

static void test_invalid_hostname_chars() {
    assert(!is_valid_hostname("my_host")); // underscore
    assert(!is_valid_hostname("-leading"));
    assert(!is_valid_hostname("trailing-"));
    assert(is_valid_hostname("wmbus-gw"));
    assert(is_valid_hostname("gw1"));
    printf("  PASS: hostname character validation\n");
}

static void test_invalid_mqtt_port() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
    cfg.mqtt.port = 0;
    auto result = validate_config(cfg);
    assert(!result.valid);
    printf("  PASS: MQTT port 0 fails\n");
}

static void test_mqtt_disabled_skips_host_check() {
    auto cfg = AppConfig::make_default();
    cfg.mqtt.enabled = false;
    cfg.mqtt.host[0] = '\0';
    auto result = validate_config(cfg);
    assert(result.valid);
    printf("  PASS: disabled MQTT skips host check\n");
}

static void test_frequency_out_of_range() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
    cfg.radio.frequency_khz = 900000;
    auto result = validate_config(cfg);
    assert(!result.valid);
    printf("  PASS: frequency out of range fails\n");
}

static void test_session_timeout_bounds() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);

    cfg.auth.session_timeout_s = 30; // Too low
    auto r1 = validate_config(cfg);
    assert(!r1.valid);

    cfg.auth.session_timeout_s = 100000; // Too high
    auto r2 = validate_config(cfg);
    assert(!r2.valid);

    cfg.auth.session_timeout_s = 3600; // OK
    auto r3 = validate_config(cfg);
    assert(r3.valid);
    printf("  PASS: session timeout bounds\n");
}

static void test_qos_out_of_range() {
    auto cfg = AppConfig::make_default();
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
    cfg.mqtt.qos = 5;
    auto result = validate_config(cfg);
    assert(!result.valid);
    printf("  PASS: QoS > 2 fails\n");
}

int main() {
    printf("=== test_config_validation ===\n");
    test_default_config_is_valid();
    test_valid_complete_config();
    test_empty_device_name_fails();
    test_empty_hostname_fails();
    test_invalid_hostname_chars();
    test_invalid_mqtt_port();
    test_mqtt_disabled_skips_host_check();
    test_frequency_out_of_range();
    test_session_timeout_bounds();
    test_qos_out_of_range();
    printf("All config validation tests passed.\n");
    return 0;
}
