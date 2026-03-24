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
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include "telegram_router/telegram_router.hpp"
#include "dedup_service/dedup_service.hpp"
#include "auth_service/auth_service.hpp"
#include "http_server/http_server.hpp"
#include "api_handlers/api_handlers.hpp"
#include "ota_manager/ota_manager.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "metrics_service/metrics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "watchdog_service/watchdog_service.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"
#include "support_bundle_service/support_bundle_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>
#include <ctime>
#include <string>

static const char* TAG = "app_core";

// FreeRTOS queue handles
static QueueHandle_t frame_queue = nullptr;
static QueueHandle_t mqtt_outbox = nullptr;

struct MqttOutboxItem {
    char topic[128];
    char payload[640];
};

// Board-specific SPI pin assignments for CC1101.
// These must match the physical wiring of the target board.
// TODO: Move to a board_config header or Kconfig before supporting multiple HW revisions.
static constexpr radio_cc1101::SpiPins kDefaultSpiPins = {
    .mosi = 23,
    .miso = 19,
    .sck = 18,
    .cs = 5,
    .gdo0 = 4,
    .gdo2 = 2,
};

static bool enqueue_mqtt(const std::string& topic, const std::string& payload) {
    if (!mqtt_outbox) return false;
    MqttOutboxItem item{};
    std::strncpy(item.topic, topic.c_str(), sizeof(item.topic) - 1);
    std::strncpy(item.payload, payload.c_str(), sizeof(item.payload) - 1);
    return xQueueSend(mqtt_outbox, &item, pdMS_TO_TICKS(10)) == pdTRUE;
}

// -- Task functions --

static void radio_rx_task(void* /*param*/) {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto& rsm = radio_state_machine::RadioStateMachine::instance();

    while (true) {
        rsm.tick();

        auto result = radio.read_frame();
        if (result.is_ok()) {
            auto frame = result.value();
            if (frame_queue) {
                xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(10));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void pipeline_task(void* /*param*/) {
    auto& router = telegram_router::TelegramRouter::instance();
    uint32_t rx_count = 0;

    radio_cc1101::RawRadioFrame raw{};
    while (true) {
        if (frame_queue && xQueueReceive(frame_queue, &raw, pdMS_TO_TICKS(100)) == pdTRUE) {
            rx_count++;

            auto& ntp = ntp_service::NtpService::instance();
            int64_t ts = ntp.now_epoch_ms();

            auto frame_result = wmbus_minimal_pipeline::WmbusPipeline::from_radio_frame(raw, ts, rx_count);
            if (frame_result.is_error()) continue;

            auto& frame = frame_result.value();
            auto route = router.route(frame);

            auto cfg = config_store::ConfigStore::instance().config();

            if (route.publish_raw) {
                char ts_str[32] = "1970-01-01T00:00:00Z";
                if (ts > 0) {
                    time_t sec = static_cast<time_t>(ts / 1000);
                    struct tm t;
                    gmtime_r(&sec, &t);
                    strftime(ts_str, sizeof(ts_str), "%Y-%m-%dT%H:%M:%SZ", &t);
                }

                enqueue_mqtt(
                    mqtt_service::topic_raw_frame(cfg.mqtt.prefix, cfg.device.hostname),
                    mqtt_service::payload_raw_frame(
                        frame.raw_hex.c_str(),
                        frame.metadata.frame_length,
                        frame.metadata.rssi_dbm,
                        frame.metadata.lqi,
                        frame.metadata.crc_ok,
                        ts_str,
                        rx_count));
            }

            if (route.publish_event && route.event_message) {
                enqueue_mqtt(
                    mqtt_service::topic_events(cfg.mqtt.prefix, cfg.device.hostname),
                    mqtt_service::payload_event(
                        "radio_event", "warning", route.event_message, ""));
            }
        }
    }
}

static void mqtt_task(void* /*param*/) {
    auto& mqtt = mqtt_service::MqttService::instance();

    MqttOutboxItem item{};
    while (true) {
        if (mqtt_outbox && xQueueReceive(mqtt_outbox, &item, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (mqtt.is_connected()) {
                mqtt.publish(item.topic, item.payload, 0, false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void health_task(void* /*param*/) {
    while (true) {
        watchdog_service::WatchdogService::instance().feed();

        auto& wifi = wifi_manager::WifiManager::instance();
        auto& mqtt = mqtt_service::MqttService::instance();
        auto& health = health_monitor::HealthMonitor::instance();

        if (wifi.state() == wifi_manager::WifiState::Connected &&
            mqtt.is_connected()) {
            health.report_healthy();
        } else if (wifi.state() == wifi_manager::WifiState::Disconnected) {
            health.report_warning("WiFi disconnected");
        } else if (!mqtt.is_connected()) {
            health.report_warning("MQTT disconnected");
        }

        if (mqtt.is_connected()) {
            auto cfg = config_store::ConfigStore::instance().config();
            auto metrics_res = metrics_service::MetricsService::instance().snapshot();
            if (metrics_res.is_ok()) {
                const auto& m = metrics_res.value();
                const auto& rc = radio_cc1101::RadioCc1101::instance().counters();
                const auto& tc = telegram_router::TelegramRouter::instance().counters();
                auto ms = mqtt.status();
                bool rx_active = radio_state_machine::RadioStateMachine::instance().state()
                                     == radio_state_machine::RsmState::Receiving;

                enqueue_mqtt(
                    mqtt_service::topic_telemetry(cfg.mqtt.prefix, cfg.device.hostname),
                    mqtt_service::payload_telemetry(
                        static_cast<uint32_t>(m.uptime_s),
                        m.free_heap_bytes, m.min_free_heap_bytes,
                        wifi.status().rssi_dbm,
                        mqtt.is_connected() ? "connected" : "disconnected",
                        rx_active ? "rx_active" : "idle",
                        rc.frames_received, tc.frames_published,
                        tc.frames_duplicate, rc.frames_crc_fail,
                        ms.publish_count, ms.publish_failures,
                        ""));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

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

void AppCore::create_runtime_tasks() {
#ifndef HOST_TEST_BUILD
    frame_queue = xQueueCreate(16, sizeof(radio_cc1101::RawRadioFrame));
    mqtt_outbox = xQueueCreate(32, sizeof(MqttOutboxItem));

    // Init radio
    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    rsm.initialize(kDefaultSpiPins);
    rsm.start_receiving();

    xTaskCreatePinnedToCore(radio_rx_task, "radio_rx", 4096, nullptr, 10, nullptr, 1);
    xTaskCreatePinnedToCore(pipeline_task, "pipeline", 4096, nullptr, 7, nullptr, 0);
    xTaskCreatePinnedToCore(mqtt_task, "mqtt_pub", 6144, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(health_task, "health", 4096, nullptr, 3, nullptr, 0);

    ESP_LOGI(TAG, "Runtime tasks created (radio_rx@Core1, pipeline@Core0, mqtt_pub@Core0, health@Core0)");
#endif
}

} // namespace app_core
