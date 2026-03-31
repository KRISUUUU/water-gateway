#include "host_test_stubs.hpp"

#include "config_store/config_store.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include <cassert>
#include <cstdio>

namespace {

void disable_mqtt_for_validation(config_store::AppConfig& cfg) {
    cfg.mqtt.enabled = false;
}

void test_start_sta_uses_current_configured_retry_limit() {
    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    cfg.wifi.max_retries = 3;

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto start = wifi_manager::WifiManager::instance().start_sta("HomeWiFi", "secret");
    assert(start.is_ok());
    assert(wifi_manager::WifiManager::instance().configured_max_retries_for_test() == 3);

    cfg.wifi.max_retries = 7;
    save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    start = wifi_manager::WifiManager::instance().start_sta("HomeWiFi", "secret");
    assert(start.is_ok());
    assert(wifi_manager::WifiManager::instance().configured_max_retries_for_test() == 7);

    std::printf("  PASS: start_sta uses current wifi.max_retries from config\n");
}

} // namespace

int main() {
    std::printf("=== test_wifi_manager ===\n");

    auto cfg_init = config_store::ConfigStore::instance().initialize();
    assert(cfg_init.is_ok() || cfg_init.error() == common::ErrorCode::AlreadyInitialized);

    auto wifi_init = wifi_manager::WifiManager::instance().initialize();
    assert(wifi_init.is_ok() || wifi_init.error() == common::ErrorCode::AlreadyInitialized);

    test_start_sta_uses_current_configured_retry_limit();

    std::printf("All wifi manager tests passed.\n");
    return 0;
}
