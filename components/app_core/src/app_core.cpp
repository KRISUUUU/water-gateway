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
        ESP_LOGE(TAG, "Foundation init failed, halting (%s/%d)",
                 common::error_code_to_string(result.error()),
                 static_cast<int>(result.error()));
#endif
        return;
    }

    auto mode = determine_start_mode();
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Startup mode selected: %s",
             mode == common::SystemMode::Provisioning ? "provisioning" : "normal");
#endif

    if (mode == common::SystemMode::Provisioning) {
        auto prov_result = start_provisioning();
        if (prov_result.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG, "Provisioning startup failed (%s/%d)",
                     common::error_code_to_string(prov_result.error()),
                     static_cast<int>(prov_result.error()));
#endif
        }
    } else {
        auto rt_result = start_normal_runtime();
        if (rt_result.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG, "Runtime start failed (%s/%d)",
                     common::error_code_to_string(rt_result.error()),
                     static_cast<int>(rt_result.error()));
#endif
        }
    }
}

common::Result<void> AppCore::initialize_foundations() {
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Foundations: event bus");
#endif
    auto& bus = event_bus::EventBus::instance();
    auto bus_result = bus.initialize();
    if (bus_result.is_error()) {
        return bus_result;
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Foundations: storage");
#endif
    auto& storage = storage_service::StorageService::instance();
    auto storage_result = storage.initialize();
    if (storage_result.is_error()) {
        return storage_result;
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Foundations: config");
#endif
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
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Provisioning: WiFi manager init");
#endif
    auto& wifi = wifi_manager::WifiManager::instance();
    auto wifi_init_result = wifi.initialize();
    if (wifi_init_result.is_error()) {
        return wifi_init_result;
    }

    auto& prov = provisioning_manager::ProvisioningManager::instance();
    auto prov_init_result = prov.initialize();
    if (prov_init_result.is_error()) {
        return prov_init_result;
    }
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Provisioning: starting AP + provisioning manager");
#endif
    auto result = prov.start();

    if (result.is_error()) {
        return result;
    }

    // Provisioning path still needs auth/session bootstrap, because
    // API endpoints are protected and first login is used to submit config.
    auto& auth = auth_service::AuthService::instance();
    auto auth_init = auth.initialize();
    if (auth_init.is_error()) {
        return auth_init;
    }

    // Start HTTP server for provisioning page
    auto& http = http_server::HttpServer::instance();
    auto http_init = http.initialize();
    if (http_init.is_error()) {
        return http_init;
    }
#ifndef HOST_TEST_BUILD
    auto http_start = http.start(80);
    if (http_start.is_error()) {
        return http_start;
    }
    api_handlers::register_all_handlers(http.native_handle());
    auto static_handler = http.register_static_web_handler();
    if (static_handler.is_error()) {
        return static_handler;
    }
#endif

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Provisioning mode active. Connect to WMBus-GW-Setup AP and open http://192.168.4.1/");
#endif
    return common::Result<void>::ok();
}

common::Result<void> AppCore::start_normal_runtime() {
    auto cfg = config_store::ConfigStore::instance().config();

    // Init WiFi and connect
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: WiFi init + STA start");
#endif
    auto& wifi = wifi_manager::WifiManager::instance();
    auto wifi_init = wifi.initialize();
    if (wifi_init.is_error()) {
        return wifi_init;
    }
    auto wifi_start = wifi.start_sta(cfg.wifi.ssid, cfg.wifi.password);
    if (wifi_start.is_error()) {
        return wifi_start;
    }

    // Init NTP
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: NTP init");
#endif
    auto& ntp = ntp_service::NtpService::instance();
    auto ntp_init = ntp.initialize();
    if (ntp_init.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "NTP initialize failed, continuing (%s/%d)",
                 common::error_code_to_string(ntp_init.error()),
                 static_cast<int>(ntp_init.error()));
#endif
    } else {
        auto ntp_start = ntp.start();
        if (ntp_start.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG, "NTP start failed, continuing (%s/%d)",
                     common::error_code_to_string(ntp_start.error()),
                     static_cast<int>(ntp_start.error()));
#endif
        }
    }

    // Init mDNS
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: mDNS init");
#endif
    auto& mdns = mdns_service::MdnsService::instance();
    auto mdns_init = mdns.initialize();
    if (mdns_init.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "mDNS initialize failed, continuing (%s/%d)",
                 common::error_code_to_string(mdns_init.error()),
                 static_cast<int>(mdns_init.error()));
#endif
    } else {
        auto mdns_start = mdns.start(cfg.device.hostname);
        if (mdns_start.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG, "mDNS start failed, continuing (%s/%d)",
                     common::error_code_to_string(mdns_start.error()),
                     static_cast<int>(mdns_start.error()));
#endif
        }
    }

    // Init MQTT
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: MQTT init");
#endif
    auto& mqtt = mqtt_service::MqttService::instance();
    auto mqtt_init = mqtt.initialize();
    if (mqtt_init.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "MQTT initialize failed, continuing (%s/%d)",
                 common::error_code_to_string(mqtt_init.error()),
                 static_cast<int>(mqtt_init.error()));
#endif
    }

    if (mqtt_init.is_ok() && cfg.mqtt.enabled && cfg.mqtt.host[0] != '\0') {
        // Set LWT
        std::string lwt_topic = mqtt_service::topic_status(cfg.mqtt.prefix, cfg.device.hostname);
        std::string lwt_payload = mqtt_service::payload_status_offline();
        mqtt.set_last_will(lwt_topic.c_str(), lwt_payload.c_str());

        auto mqtt_connect = mqtt.connect(cfg.mqtt.host, cfg.mqtt.port,
                                         cfg.mqtt.username, cfg.mqtt.password,
                                         cfg.mqtt.client_id, cfg.mqtt.use_tls);
        if (mqtt_connect.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG, "MQTT connect failed, runtime continues (%s/%d)",
                     common::error_code_to_string(mqtt_connect.error()),
                     static_cast<int>(mqtt_connect.error()));
#endif
        }
    } else if (cfg.mqtt.enabled) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "MQTT enabled but host is empty, skipping MQTT connect");
#endif
    }

    // Init Auth
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: auth init");
#endif
    auto& auth = auth_service::AuthService::instance();
    auto auth_init = auth.initialize();
    if (auth_init.is_error()) {
        return auth_init;
    }

    // Init HTTP server
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: HTTP init + handler registration");
#endif
    auto& http = http_server::HttpServer::instance();
    auto http_init = http.initialize();
    if (http_init.is_error()) {
        return http_init;
    }
#ifndef HOST_TEST_BUILD
    auto http_start = http.start(80);
    if (http_start.is_error()) {
        return http_start;
    }
    api_handlers::register_all_handlers(http.native_handle());
    auto static_handler = http.register_static_web_handler();
    if (static_handler.is_error()) {
        return static_handler;
    }
#endif

    // Init OTA
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: OTA init + boot-valid");
#endif
    auto& ota = ota_manager::OtaManager::instance();
    auto ota_init = ota.initialize();
    if (ota_init.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "OTA initialize failed, continuing (%s/%d)",
                 common::error_code_to_string(ota_init.error()),
                 static_cast<int>(ota_init.error()));
#endif
    } else {
        auto boot_valid = ota.mark_boot_valid();
        if (boot_valid.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG, "mark_boot_valid failed, continuing (%s/%d)",
                     common::error_code_to_string(boot_valid.error()),
                     static_cast<int>(boot_valid.error()));
#endif
        }
    }

    // Init watchdog
#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "[BOOT] Normal mode: watchdog init");
#endif
    auto& wd = watchdog_service::WatchdogService::instance();
    auto wd_init = wd.initialize();
    if (wd_init.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Watchdog initialize failed, continuing (%s/%d)",
                 common::error_code_to_string(wd_init.error()),
                 static_cast<int>(wd_init.error()));
#endif
    }

    // Create runtime tasks
    auto tasks_result = create_runtime_tasks();
    if (tasks_result.is_error()) {
        return tasks_result;
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Normal runtime started");
#endif
    return common::Result<void>::ok();
}

} // namespace app_core
