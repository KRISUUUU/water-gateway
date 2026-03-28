#include "app_core/app_core.hpp"

#ifndef HOST_TEST_BUILD

#include "board_config/board_config.hpp"
#include "config_store/config_store.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "ntp_service/ntp_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "telegram_router/telegram_router.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>

namespace {

static const char* TAG = "app_runtime";

static QueueHandle_t frame_queue = nullptr;
static QueueHandle_t mqtt_outbox = nullptr;

struct MqttOutboxItem {
    char topic[128];
    char payload[896];
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

static std::string derive_meter_key(const wmbus_minimal_pipeline::WmbusFrame& frame) {
    return frame.identity_key();
}

static void radio_rx_task(void* /*param*/) {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto& rsm = radio_state_machine::RadioStateMachine::instance();

    while (true) {
        rsm.tick();

        auto result = radio.read_frame();
        if (result.is_ok() && frame_queue) {
            auto frame = result.value();
            // Q1 fix: check return value — a full queue silently dropped frames before.
            if (xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
                radio.note_frame_queue_full();
                ESP_LOGW(TAG, "frame_queue full — radio frame dropped (total: %lu)",
                         (unsigned long)radio.counters().frames_dropped_queue_full);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void pipeline_task(void* /*param*/) {
    auto& router = telegram_router::TelegramRouter::instance();
    uint32_t rx_count = 0;
    // Q1 fix: track mqtt_outbox overflow drops; log first occurrence and every 100th.
    uint32_t mqtt_drop_count = 0;

    radio_cc1101::RawRadioFrame raw{};
    while (true) {
        if (frame_queue && xQueueReceive(frame_queue, &raw, pdMS_TO_TICKS(100)) == pdTRUE) {
            rx_count++;

            const int64_t ts = ntp_service::NtpService::instance().now_epoch_ms();

            auto frame_result =
                wmbus_minimal_pipeline::WmbusPipeline::from_radio_frame(raw, ts, rx_count);
            if (frame_result.is_error()) {
                continue;
            }

            const auto& frame = frame_result.value();
            const auto route = router.route(frame);
            const auto cfg = config_store::ConfigStore::instance().config();
            const bool duplicate =
                route.decision == telegram_router::RouteDecision::SuppressDuplicate;
            meter_registry::MeterRegistry::instance().observe_frame(frame, duplicate);

            if (route.publish_raw) {
                char ts_str[32] = "1970-01-01T00:00:00Z";
                if (ts > 0) {
                    const time_t sec = static_cast<time_t>(ts / 1000);
                    struct tm t;
                    gmtime_r(&sec, &t);
                    strftime(ts_str, sizeof(ts_str), "%Y-%m-%dT%H:%M:%SZ", &t);
                }

                // Q1 fix: check enqueue_mqtt() return — outbox drops were silent before.
                if (!enqueue_mqtt(
                        mqtt_service::topic_raw_frame(cfg.mqtt.prefix, cfg.device.hostname),
                        mqtt_service::payload_raw_frame(
                            frame.raw_hex().c_str(), frame.metadata.frame_length,
                            frame.metadata.rssi_dbm, frame.metadata.lqi, frame.metadata.crc_ok,
                            frame.manufacturer_id(), frame.device_id(),
                            derive_meter_key(frame).c_str(), ts_str, rx_count))) {
                    mqtt_drop_count++;
                    if (mqtt_drop_count == 1 || (mqtt_drop_count % 100) == 0) {
                        ESP_LOGW(TAG, "mqtt_outbox full — frame dropped (total drops: %lu)",
                                 (unsigned long)mqtt_drop_count);
                    }
                }
            }

            if (route.publish_event && route.event_message) {
                // Q1 fix: check return; event drops are logged via the same counter.
                if (!enqueue_mqtt(
                        mqtt_service::topic_events(cfg.mqtt.prefix, cfg.device.hostname),
                        mqtt_service::payload_event("radio_event", "warning", route.event_message,
                                                    ""))) {
                    mqtt_drop_count++;
                    if (mqtt_drop_count == 1 || (mqtt_drop_count % 100) == 0) {
                        ESP_LOGW(TAG, "mqtt_outbox full — event dropped (total drops: %lu)",
                                 (unsigned long)mqtt_drop_count);
                    }
                }
            }
        }
    }
}

static void mqtt_task(void* /*param*/) {
    auto& mqtt = mqtt_service::MqttService::instance();
    auto report_outbox_state = [&mqtt](bool has_carry_item) {
        const uint32_t depth =
            mqtt_outbox ? static_cast<uint32_t>(uxQueueMessagesWaiting(mqtt_outbox)) : 0;
        const uint32_t capacity = mqtt_outbox
            ? depth + static_cast<uint32_t>(uxQueueSpacesAvailable(mqtt_outbox))
            : 0;
        mqtt.note_outbox_state(depth, capacity, has_carry_item);
    };

    // Carry-over item: holds one dequeued message that could not be published
    // (broker disconnected or publish() failed). Retried on the next cycle.
    // This prevents silent drops during brief MQTT reconnects.
    bool has_carry = false;
    MqttOutboxItem carry{};
    uint32_t held_count = 0;
    uint32_t retried_count = 0;
    uint32_t publish_failed_count = 0;

    while (true) {
        MqttOutboxItem* item_ptr = nullptr;
        MqttOutboxItem fresh{};

        if (has_carry) {
            item_ptr = &carry;
        } else if (mqtt_outbox &&
                   xQueueReceive(mqtt_outbox, &fresh, pdMS_TO_TICKS(500)) == pdTRUE) {
            item_ptr = &fresh;
        }

        if (item_ptr) {
            if (!mqtt.is_connected()) {
                // Hold for retry - do NOT dequeue further until reconnected.
                if (!has_carry) {
                    carry = *item_ptr;
                    has_carry = true;
                    held_count++;
                    mqtt.note_runtime_hold();
                    ESP_LOGD(TAG, "MQTT disconnected - item held (held:%lu)", (unsigned long)held_count);
                }
                report_outbox_state(has_carry);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            // Connected - attempt publish.
            if (has_carry) {
                retried_count++;
                mqtt.note_runtime_retry();
                ESP_LOGD(TAG, "MQTT retry carry item (retried:%lu)", (unsigned long)retried_count);
            }
            auto pub_result = mqtt.publish(item_ptr->topic, item_ptr->payload, 0, false);
            if (pub_result.is_ok()) {
                has_carry = false;
                report_outbox_state(has_carry);
            } else {
                // publish() failed despite is_connected() being true (race on disconnect).
                // Keep carry for retry; do not dequeue next item.
                publish_failed_count++;
                mqtt.note_runtime_retry_failure();
                if (!has_carry) {
                    carry = *item_ptr;
                    has_carry = true;
                    mqtt.note_runtime_hold();
                }
                report_outbox_state(has_carry);
                ESP_LOGW(TAG, "MQTT publish() failed - item held for retry (pub_fail:%lu)",
                         (unsigned long)publish_failed_count);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }

        report_outbox_state(has_carry);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void health_task(void* /*param*/) {
    // Low-frequency telemetry (30s cadence) must not subscribe to TWDT: default timeout is 5s.
    while (true) {
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
                const bool rx_active = radio_state_machine::RadioStateMachine::instance().state() ==
                                       radio_state_machine::RsmState::Receiving;

                enqueue_mqtt(mqtt_service::topic_telemetry(cfg.mqtt.prefix, cfg.device.hostname),
                             mqtt_service::payload_telemetry(
                                 static_cast<uint32_t>(m.uptime_s), m.free_heap_bytes,
                                 m.min_free_heap_bytes, wifi.status().rssi_dbm,
                                 mqtt.is_connected() ? "connected" : "disconnected",
                                 rx_active ? "rx_active" : "idle", rc.frames_received,
                                 tc.frames_published, tc.frames_duplicate, rc.frames_crc_fail,
                                 ms.publish_count, ms.publish_failures, ""));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

} // namespace

namespace app_core {

common::Result<void> AppCore::create_runtime_tasks() {
    frame_queue = xQueueCreate(16, sizeof(radio_cc1101::RawRadioFrame));
    mqtt_outbox = xQueueCreate(32, sizeof(MqttOutboxItem));
    if (!frame_queue || !mqtt_outbox) {
        ESP_LOGE(TAG, "Runtime queue allocation failed (frame_queue=%p mqtt_outbox=%p)",
                 frame_queue, mqtt_outbox);
        return common::Result<void>::error(common::ErrorCode::BufferFull);
    }

    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    const auto pins = board_config::default_cc1101_pins();
    const auto bus = board_config::default_cc1101_spi_bus_config();
    ESP_LOGI(TAG, "Board CC1101 pins: MOSI=%d MISO=%d SCK=%d CS=%d GDO0=%d GDO2=%d", pins.mosi,
             pins.miso, pins.sck, pins.cs, pins.gdo0, pins.gdo2);
    ESP_LOGI(TAG, "CC1101 SPI bus: host=%d clock_hz=%lu max_transfer=%d", bus.host_id,
             static_cast<unsigned long>(bus.clock_hz), bus.max_transfer_size);

    auto rsm_init = rsm.initialize(pins, bus);
    if (rsm_init.is_error()) {
        ESP_LOGE(TAG, "Radio state machine initialize failed (%s/%d)",
                 common::error_code_to_string(rsm_init.error()),
                 static_cast<int>(rsm_init.error()));
        return rsm_init;
    }

    auto rx_start = rsm.start_receiving();
    if (rx_start.is_error()) {
        ESP_LOGE(TAG, "Radio RX start failed (%s/%d)",
                 common::error_code_to_string(rx_start.error()),
                 static_cast<int>(rx_start.error()));
        return rx_start;
    }

    if (xTaskCreatePinnedToCore(radio_rx_task, "radio_rx", 4096, nullptr, 10, nullptr, 1) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: radio_rx");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(pipeline_task, "pipeline", 4096, nullptr, 7, nullptr, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: pipeline");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(mqtt_task, "mqtt_pub", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: mqtt_pub");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(health_task, "health", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: health");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    ESP_LOGI(
        TAG,
        "Runtime tasks created (radio_rx@Core1, pipeline@Core0, mqtt_pub@Core0, health@Core0)");
    return common::Result<void>::ok();
}

} // namespace app_core

#else

namespace app_core {

common::Result<void> AppCore::create_runtime_tasks() {
    return common::Result<void>::ok();
}

} // namespace app_core

#endif // HOST_TEST_BUILD
