#include "app_core/app_core.hpp"

#ifndef HOST_TEST_BUILD

#include "board_config/board_config.hpp"
#include "config_store/config_store.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "ntp_service/ntp_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "telegram_router/telegram_router.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include "watchdog_service/watchdog_service.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstring>
#include <ctime>
#include <string>

namespace {

static const char* TAG = "app_runtime";

static QueueHandle_t frame_queue = nullptr;
static QueueHandle_t mqtt_outbox = nullptr;

struct MqttOutboxItem {
    char topic[128];
    char payload[640];
};

static bool enqueue_mqtt(const std::string& topic, const std::string& payload) {
    if (!mqtt_outbox) {
        return false;
    }
    MqttOutboxItem item{};
    std::strncpy(item.topic, topic.c_str(), sizeof(item.topic) - 1);
    std::strncpy(item.payload, payload.c_str(), sizeof(item.payload) - 1);
    return xQueueSend(mqtt_outbox, &item, pdMS_TO_TICKS(10)) == pdTRUE;
}

static void radio_rx_task(void* /*param*/) {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto& rsm = radio_state_machine::RadioStateMachine::instance();

    while (true) {
        rsm.tick();

        auto result = radio.read_frame();
        if (result.is_ok() && frame_queue) {
            auto frame = result.value();
            xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(10));
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

            const int64_t ts = ntp_service::NtpService::instance().now_epoch_ms();

            auto frame_result = wmbus_minimal_pipeline::WmbusPipeline::from_radio_frame(raw, ts, rx_count);
            if (frame_result.is_error()) {
                continue;
            }

            const auto& frame = frame_result.value();
            const auto route = router.route(frame);
            const auto cfg = config_store::ConfigStore::instance().config();

            if (route.publish_raw) {
                char ts_str[32] = "1970-01-01T00:00:00Z";
                if (ts > 0) {
                    const time_t sec = static_cast<time_t>(ts / 1000);
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
                    mqtt_service::payload_event("radio_event", "warning", route.event_message, ""));
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

        if (wifi.state() == wifi_manager::WifiState::Connected && mqtt.is_connected()) {
            health.report_healthy();
        } else if (wifi.state() == wifi_manager::WifiState::Disconnected) {
            health.report_warning("WiFi disconnected");
        } else if (!mqtt.is_connected()) {
            health.report_warning("MQTT disconnected");
        }

        if (mqtt.is_connected()) {
            const auto cfg = config_store::ConfigStore::instance().config();
            auto metrics_res = metrics_service::MetricsService::instance().snapshot();
            if (metrics_res.is_ok()) {
                const auto& m = metrics_res.value();
                const auto& rc = radio_cc1101::RadioCc1101::instance().counters();
                const auto& tc = telegram_router::TelegramRouter::instance().counters();
                const auto ms = mqtt.status();
                const bool rx_active = radio_state_machine::RadioStateMachine::instance().state()
                                       == radio_state_machine::RsmState::Receiving;

                enqueue_mqtt(
                    mqtt_service::topic_telemetry(cfg.mqtt.prefix, cfg.device.hostname),
                    mqtt_service::payload_telemetry(
                        static_cast<uint32_t>(m.uptime_s),
                        m.free_heap_bytes,
                        m.min_free_heap_bytes,
                        wifi.status().rssi_dbm,
                        mqtt.is_connected() ? "connected" : "disconnected",
                        rx_active ? "rx_active" : "idle",
                        rc.frames_received,
                        tc.frames_published,
                        tc.frames_duplicate,
                        rc.frames_crc_fail,
                        ms.publish_count,
                        ms.publish_failures,
                        ""));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

} // namespace

namespace app_core {

void AppCore::create_runtime_tasks() {
    frame_queue = xQueueCreate(16, sizeof(radio_cc1101::RawRadioFrame));
    mqtt_outbox = xQueueCreate(32, sizeof(MqttOutboxItem));

    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    const auto pins = board_config::default_cc1101_pins();
    rsm.initialize(pins);
    rsm.start_receiving();

    xTaskCreatePinnedToCore(radio_rx_task, "radio_rx", 4096, nullptr, 10, nullptr, 1);
    xTaskCreatePinnedToCore(pipeline_task, "pipeline", 4096, nullptr, 7, nullptr, 0);
    xTaskCreatePinnedToCore(mqtt_task, "mqtt_pub", 6144, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(health_task, "health", 4096, nullptr, 3, nullptr, 0);

    ESP_LOGI(TAG, "Runtime tasks created (radio_rx@Core1, pipeline@Core0, mqtt_pub@Core0, health@Core0)");
}

} // namespace app_core

#else

namespace app_core {

void AppCore::create_runtime_tasks() {}

} // namespace app_core

#endif // HOST_TEST_BUILD
