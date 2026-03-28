#include "app_core/app_core.hpp"

#include "api_handlers/api_handlers.hpp"
#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include "event_bus/event_bus.hpp"
#include "http_server/http_server.hpp"
#include "mdns_service/mdns_service.hpp"
#include "meter_registry/meter_registry.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "ntp_service/ntp_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "provisioning_manager/provisioning_manager.hpp"
#include "storage_service/storage_service.hpp"
#include "watchdog_service/watchdog_service.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include <string>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
static const char* TAG = "app_core";
#define APP_CORE_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define APP_CORE_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define APP_CORE_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#else
#define APP_CORE_LOGI(...) ((void)0)
#define APP_CORE_LOGW(...) ((void)0)
#define APP_CORE_LOGE(...) ((void)0)
#endif // HOST_TEST_BUILD

namespace app_core {

namespace {

void log_error_result(const char* message, common::ErrorCode error) {
    APP_CORE_LOGE("%s (%s/%d)", message, common::error_code_to_string(error),
                  static_cast<int>(error));
}

void log_warning_result(const char* message, common::ErrorCode error) {
    APP_CORE_LOGW("%s (%s/%d)", message, common::error_code_to_string(error),
                  static_cast<int>(error));
}

common::Result<void> start_http_ui(http_server::HttpServer& http) {
#ifdef HOST_TEST_BUILD
    (void)http;
    return common::Result<void>::ok();
#else
    auto http_start = http.start(80);
    if (http_start.is_error()) {
        return http_start;
    }
    api_handlers::register_all_handlers(http.native_handle());
    return http.register_static_web_handler();
#endif
}

} // namespace

void AppCore::start() {
    APP_CORE_LOGI("=== WMBus Gateway Starting ===");

    auto result = initialize_foundations();
    if (result.is_error()) {
        log_error_result("Foundation init failed, halting", result.error());
        return;
    }

    auto mode = determine_start_mode();
    APP_CORE_LOGI("Startup mode selected: %s",
                  mode == common::SystemMode::Provisioning ? "provisioning" : "normal");

    if (mode == common::SystemMode::Provisioning) {
        auto prov_result = start_provisioning();
        if (prov_result.is_error()) {
            log_error_result("Provisioning startup failed", prov_result.error());
        }
    } else {
        auto rt_result = start_normal_runtime();
        if (rt_result.is_error()) {
            log_error_result("Runtime start failed", rt_result.error());
        }
    }
}

common::Result<void> AppCore::initialize_foundations() {
    APP_CORE_LOGI("[BOOT] Foundations: event bus");
    auto& bus = event_bus::EventBus::instance();
    auto bus_result = bus.initialize();
    if (bus_result.is_error()) {
        return bus_result;
    }

    APP_CORE_LOGI("[BOOT] Foundations: storage");
    auto& storage = storage_service::StorageService::instance();
    auto storage_result = storage.initialize();
    if (storage_result.is_error()) {
        return storage_result;
    }

    APP_CORE_LOGI("[BOOT] Foundations: meter registry");
    auto& registry = meter_registry::MeterRegistry::instance();
    auto registry_result = registry.initialize();
    if (registry_result.is_error()) {
        log_warning_result("Meter registry initialize failed, continuing", registry_result.error());
    }

    APP_CORE_LOGI("[BOOT] Foundations: config");
    auto& config = config_store::ConfigStore::instance();
    auto cfg_result = config.initialize();
    if (cfg_result.is_error()) {
        return cfg_result;
    }

    APP_CORE_LOGI("Foundations initialized");
    return common::Result<void>::ok();
}

common::SystemMode AppCore::determine_start_mode() {
    if (!config_store::ConfigStore::instance().wifi_is_configured()) {
        APP_CORE_LOGI("WiFi not configured, entering provisioning mode");
        return common::SystemMode::Provisioning;
    }

    return common::SystemMode::Normal;
}

common::Result<void> AppCore::start_provisioning() {
    APP_CORE_LOGI("[BOOT] Provisioning: WiFi manager init");
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
    APP_CORE_LOGI("[BOOT] Provisioning: starting AP + provisioning manager");
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
    auto http_start = start_http_ui(http);
    if (http_start.is_error()) {
        return http_start;
    }

    APP_CORE_LOGI(
        "Provisioning mode active. Connect to WMBus-GW-Setup AP and open http://192.168.4.1/");
    return common::Result<void>::ok();
}

common::Result<void> AppCore::start_normal_runtime() {
    auto cfg = config_store::ConfigStore::instance().config();

    // Init WiFi and connect
    APP_CORE_LOGI("[BOOT] Normal mode: WiFi init + STA start");
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
    APP_CORE_LOGI("[BOOT] Normal mode: NTP init");
    auto& ntp = ntp_service::NtpService::instance();
    auto ntp_init = ntp.initialize();
    if (ntp_init.is_error()) {
        log_warning_result("NTP initialize failed, continuing", ntp_init.error());
    } else {
        auto ntp_start = ntp.start();
        if (ntp_start.is_error()) {
            log_warning_result("NTP start failed, continuing", ntp_start.error());
        }
    }

    // Init mDNS
    APP_CORE_LOGI("[BOOT] Normal mode: mDNS init");
    auto& mdns = mdns_service::MdnsService::instance();
    auto mdns_init = mdns.initialize();
    if (mdns_init.is_error()) {
        log_warning_result("mDNS initialize failed, continuing", mdns_init.error());
    } else {
        auto mdns_start = mdns.start(cfg.device.hostname);
        if (mdns_start.is_error()) {
            log_warning_result("mDNS start failed, continuing", mdns_start.error());
        }
    }

    // Init MQTT
    APP_CORE_LOGI("[BOOT] Normal mode: MQTT init");
    auto& mqtt = mqtt_service::MqttService::instance();
    auto mqtt_init = mqtt.initialize();
    if (mqtt_init.is_error()) {
        log_warning_result("MQTT initialize failed, continuing", mqtt_init.error());
    }

    if (mqtt_init.is_ok() && cfg.mqtt.enabled && cfg.mqtt.host[0] != '\0') {
        // Set LWT
        std::string lwt_topic = mqtt_service::topic_status(cfg.mqtt.prefix, cfg.device.hostname);
        std::string lwt_payload = mqtt_service::payload_status_offline();
        mqtt.set_last_will(lwt_topic.c_str(), lwt_payload.c_str());

        auto mqtt_connect = mqtt.connect(cfg.mqtt.host, cfg.mqtt.port, cfg.mqtt.username,
                                         cfg.mqtt.password, cfg.mqtt.client_id, cfg.mqtt.use_tls);
        if (mqtt_connect.is_error()) {
            log_warning_result("MQTT connect failed, runtime continues", mqtt_connect.error());
        }
    } else if (cfg.mqtt.enabled) {
        APP_CORE_LOGW("MQTT enabled but host is empty, skipping MQTT connect");
    }

    // Init Auth
    APP_CORE_LOGI("[BOOT] Normal mode: auth init");
    auto& auth = auth_service::AuthService::instance();
    auto auth_init = auth.initialize();
    if (auth_init.is_error()) {
        return auth_init;
    }

    // Init HTTP server
    APP_CORE_LOGI("[BOOT] Normal mode: HTTP init + handler registration");
    auto& http = http_server::HttpServer::instance();
    auto http_init = http.initialize();
    if (http_init.is_error()) {
        return http_init;
    }
    auto http_start = start_http_ui(http);
    if (http_start.is_error()) {
        return http_start;
    }

    // Init OTA
    APP_CORE_LOGI("[BOOT] Normal mode: OTA init + boot-valid");
    auto& ota = ota_manager::OtaManager::instance();
    auto ota_init = ota.initialize();
    if (ota_init.is_error()) {
        log_warning_result("OTA initialize failed, continuing", ota_init.error());
    } else {
        auto boot_valid = ota.mark_boot_valid();
        if (boot_valid.is_error()) {
            log_warning_result("mark_boot_valid failed, continuing", boot_valid.error());
        }
    }

    // Init watchdog
    APP_CORE_LOGI("[BOOT] Normal mode: watchdog init");
    auto& wd = watchdog_service::WatchdogService::instance();
    auto wd_init = wd.initialize();
    if (wd_init.is_error()) {
        log_warning_result("Watchdog initialize failed, continuing", wd_init.error());
    }

    // Create runtime tasks
    auto tasks_result = create_runtime_tasks();
    if (tasks_result.is_error()) {
        return tasks_result;
    }

    APP_CORE_LOGI("Normal runtime started");
    return common::Result<void>::ok();
}

} // namespace app_core
