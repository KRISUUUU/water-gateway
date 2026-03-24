#include "app_core/app_core.hpp"

#include "event_bus/event_bus.hpp"
#include "config_store/config_store.hpp"
#include "storage_service/storage_service.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "ntp_service/ntp_service.hpp"
#include "mdns_service/mdns_service.hpp"
#include "provisioning_manager/provisioning_manager.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
#include "auth_service/auth_service.hpp"
#include "http_server/http_server.hpp"
#include "api_handlers/api_handlers.hpp"
#include "ota_manager/ota_manager.hpp"
#include "watchdog_service/watchdog_service.hpp"
#include <string>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
static const char* TAG = "app_core";
#endif // HOST_TEST_BUILD

namespace app_core {

void AppCore::start() {
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "=== WMBus Gateway Starting ===");
#endif

    auto result = initialize_foundations();
    if (result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Foundation init failed, halting");
#endif
        return;
    }

    auto mode = determine_start_mode();

    if (mode == common::SystemMode::Provisioning) {
        start_provisioning();
    } else {
        auto rt_result = start_normal_runtime();
        if (rt_result.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG, "Runtime start failed");
#endif
        }
    }
}

common::Result<void> AppCore::initialize_foundations() {
    auto& bus = event_bus::EventBus::instance();
    auto bus_result = bus.initialize();
    if (bus_result.is_error()) {
        return bus_result;
    }

    auto& storage = storage_service::StorageService::instance();
    storage.initialize();

    auto& config = config_store::ConfigStore::instance();
    auto cfg_result = config.initialize();
    if (cfg_result.is_error()) {
        return cfg_result;
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Foundations initialized");
#endif
    return common::Result<void>::ok();
}

common::SystemMode AppCore::determine_start_mode() {
    auto cfg = config_store::ConfigStore::instance().config();

    if (!cfg.wifi.is_configured()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGI(TAG, "WiFi not configured, entering provisioning mode");
#endif
        return common::SystemMode::Provisioning;
    }

    return common::SystemMode::Normal;
}

common::Result<void> AppCore::start_provisioning() {
    auto& wifi = wifi_manager::WifiManager::instance();
    wifi.initialize();

    auto& prov = provisioning_manager::ProvisioningManager::instance();
    prov.initialize();
    auto result = prov.start();

    if (result.is_error()) {
        return result;
    }

    // Start HTTP server for provisioning page
    auto& http = http_server::HttpServer::instance();
    http.initialize();
#ifndef HOST_TEST_BUILD
    http.start(80);
#endif

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Provisioning mode active. Connect to WMBus-GW-Setup AP.");
#endif
    return common::Result<void>::ok();
}

common::Result<void> AppCore::start_normal_runtime() {
    auto cfg = config_store::ConfigStore::instance().config();

    // Init WiFi and connect
    auto& wifi = wifi_manager::WifiManager::instance();
    wifi.initialize();
    wifi.start_sta(cfg.wifi.ssid, cfg.wifi.password);

    // Init NTP
    auto& ntp = ntp_service::NtpService::instance();
    ntp.initialize();
    ntp.start();

    // Init mDNS
    auto& mdns = mdns_service::MdnsService::instance();
    mdns.initialize();
    mdns.start(cfg.device.hostname);

    // Init MQTT
    auto& mqtt = mqtt_service::MqttService::instance();
    mqtt.initialize();

    if (cfg.mqtt.enabled && cfg.mqtt.host[0] != '\0') {
        // Set LWT
        std::string lwt_topic = mqtt_service::topic_status(cfg.mqtt.prefix, cfg.device.hostname);
        std::string lwt_payload = mqtt_service::payload_status_offline();
        mqtt.set_last_will(lwt_topic.c_str(), lwt_payload.c_str());

        mqtt.connect(cfg.mqtt.host, cfg.mqtt.port,
                     cfg.mqtt.username, cfg.mqtt.password,
                     cfg.mqtt.client_id, cfg.mqtt.use_tls);
    }

    // Init Auth
    auto& auth = auth_service::AuthService::instance();
    auth.initialize();

    // Init HTTP server
    auto& http = http_server::HttpServer::instance();
    http.initialize();
#ifndef HOST_TEST_BUILD
    http.start(80);
    api_handlers::register_all_handlers(http.native_handle());
    http.register_static_web_handler();
#endif

    // Init OTA
    auto& ota = ota_manager::OtaManager::instance();
    ota.initialize();
    ota.mark_boot_valid();

    // Init watchdog
    auto& wd = watchdog_service::WatchdogService::instance();
    wd.initialize();

    // Create runtime tasks
    create_runtime_tasks();

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Normal runtime started");
#endif
    return common::Result<void>::ok();
}

} // namespace app_core
