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
#include "watchdog_service/watchdog_service.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <string>

namespace {

static const char* TAG = "app_runtime";

static QueueHandle_t frame_queue = nullptr;
static QueueHandle_t mqtt_outbox = nullptr;
static TaskHandle_t radio_task_handle = nullptr;
static TaskHandle_t pipeline_task_handle = nullptr;
static TaskHandle_t mqtt_task_handle = nullptr;
static TaskHandle_t health_task_handle = nullptr;
static constexpr uint32_t kFrameQueueDepth = 16;
static constexpr uint32_t kMqttOutboxDepth = 32;
static constexpr uint32_t kCriticalTaskStallMs = 5000;
static constexpr size_t kMqttOutboxTopicCapacity = 128;
static constexpr size_t kMqttOutboxPayloadCapacity = 896;

static std::atomic<uint32_t> frame_queue_peak_depth{0};
static std::atomic<uint32_t> frame_queue_max_depth{0};
static std::atomic<uint32_t> frame_enqueue_success{0};
static std::atomic<uint32_t> frame_enqueue_drop{0};
static std::atomic<uint32_t> frame_enqueue_errors{0};
static std::atomic<uint32_t> frame_queue_send_failures{0};

static std::atomic<uint32_t> mqtt_outbox_peak_depth{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_success{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_drop{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_errors{0};

static std::atomic<uint32_t> radio_loop_last_ms{0};
static std::atomic<uint32_t> pipeline_loop_last_ms{0};
static std::atomic<uint32_t> mqtt_loop_last_ms{0};
static std::atomic<uint32_t> pipeline_frames_processed{0};
static std::atomic<uint32_t> radio_read_success_count{0};
static std::atomic<uint32_t> radio_read_not_found_count{0};
static std::atomic<uint32_t> radio_read_timeout_count{0};
static std::atomic<uint32_t> radio_read_error_count{0};
static std::atomic<uint32_t> radio_not_found_streak{0};
static std::atomic<uint32_t> radio_not_found_streak_peak{0};
static std::atomic<uint32_t> radio_stall_count{0};
static std::atomic<uint32_t> pipeline_stall_count{0};
static std::atomic<uint32_t> mqtt_stall_count{0};
static std::atomic<uint32_t> watchdog_register_errors{0};
static std::atomic<uint32_t> watchdog_feed_errors{0};

static uint32_t now_ms() {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

struct MqttOutboxItem {
    char topic[kMqttOutboxTopicCapacity];
    char payload[kMqttOutboxPayloadCapacity];
};

static void update_peak(std::atomic<uint32_t>& peak, uint32_t value) {
    uint32_t current = peak.load(std::memory_order_relaxed);
    while (value > current &&
           !peak.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

static void sample_queue_levels() {
    uint32_t frame_depth = 0;
    uint32_t outbox_depth = 0;
    if (frame_queue) {
        frame_depth = static_cast<uint32_t>(uxQueueMessagesWaiting(frame_queue));
        update_peak(frame_queue_peak_depth, frame_depth);
        update_peak(frame_queue_max_depth, frame_depth);
    }
    if (mqtt_outbox) {
        outbox_depth = static_cast<uint32_t>(uxQueueMessagesWaiting(mqtt_outbox));
        update_peak(mqtt_outbox_peak_depth, outbox_depth);
        mqtt_service::MqttService::instance().report_outbox_depth(outbox_depth);
    }
    metrics_service::MetricsService::report_queue_metrics(
        frame_depth, frame_queue_peak_depth.load(std::memory_order_relaxed),
        frame_queue_max_depth.load(std::memory_order_relaxed),
        frame_enqueue_success.load(std::memory_order_relaxed),
        frame_enqueue_drop.load(std::memory_order_relaxed),
        frame_enqueue_errors.load(std::memory_order_relaxed),
        frame_queue_send_failures.load(std::memory_order_relaxed), outbox_depth,
        mqtt_outbox_peak_depth.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_success.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_drop.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_errors.load(std::memory_order_relaxed));

    const uint32_t now = now_ms();
    const uint32_t radio_age = now - radio_loop_last_ms.load(std::memory_order_relaxed);
    const uint32_t pipeline_age = now - pipeline_loop_last_ms.load(std::memory_order_relaxed);
    const uint32_t mqtt_age = now - mqtt_loop_last_ms.load(std::memory_order_relaxed);
    metrics_service::MetricsService::report_task_metrics(
        radio_age, pipeline_age, mqtt_age, pipeline_frames_processed.load(std::memory_order_relaxed),
        radio_read_success_count.load(std::memory_order_relaxed),
        radio_read_not_found_count.load(std::memory_order_relaxed),
        radio_read_timeout_count.load(std::memory_order_relaxed),
        radio_read_error_count.load(std::memory_order_relaxed),
        radio_not_found_streak.load(std::memory_order_relaxed),
        radio_not_found_streak_peak.load(std::memory_order_relaxed),
        radio_stall_count.load(std::memory_order_relaxed),
        pipeline_stall_count.load(std::memory_order_relaxed),
        mqtt_stall_count.load(std::memory_order_relaxed),
        watchdog_register_errors.load(std::memory_order_relaxed),
        watchdog_feed_errors.load(std::memory_order_relaxed));
}

static void sample_task_stack_watermarks() {
    uint32_t radio_hwm = 0;
    uint32_t pipeline_hwm = 0;
    uint32_t mqtt_hwm = 0;
    uint32_t health_hwm = 0;
    if (radio_task_handle) {
        radio_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(radio_task_handle));
    }
    if (pipeline_task_handle) {
        pipeline_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(pipeline_task_handle));
    }
    if (mqtt_task_handle) {
        mqtt_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(mqtt_task_handle));
    }
    if (health_task_handle) {
        health_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(health_task_handle));
    }
    metrics_service::MetricsService::report_task_stack_metrics(radio_hwm, pipeline_hwm, mqtt_hwm,
                                                               health_hwm);
}

static bool enqueue_mqtt(const std::string& topic, const std::string& payload) {
    auto& mqtt = mqtt_service::MqttService::instance();
    if (topic.size() >= kMqttOutboxTopicCapacity || payload.size() >= kMqttOutboxPayloadCapacity) {
        mqtt.report_outbox_enqueue_failure(true);
        const uint32_t dropped = mqtt_outbox_enqueue_drop.fetch_add(1, std::memory_order_relaxed) + 1;
        mqtt_outbox_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
        if ((dropped % 32U) == 1U) {
            ESP_LOGW(TAG,
                     "MQTT outbox oversize reject (drops=%lu topic_len=%lu payload_len=%lu)",
                     static_cast<unsigned long>(dropped), static_cast<unsigned long>(topic.size()),
                     static_cast<unsigned long>(payload.size()));
        }
        sample_queue_levels();
        return false;
    }
    if (!mqtt_outbox) {
        mqtt.report_outbox_enqueue_failure(false);
        mqtt_outbox_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    MqttOutboxItem item{};
    std::strncpy(item.topic, topic.c_str(), sizeof(item.topic) - 1);
    std::strncpy(item.payload, payload.c_str(), sizeof(item.payload) - 1);
    if (xQueueSend(mqtt_outbox, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
        mqtt_outbox_enqueue_success.fetch_add(1, std::memory_order_relaxed);
        sample_queue_levels();
        return true;
    }

    const uint32_t dropped = mqtt_outbox_enqueue_drop.fetch_add(1, std::memory_order_relaxed) + 1;
    mqtt_outbox_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
    mqtt.report_outbox_enqueue_failure(false);
    if ((dropped % 32U) == 1U) {
        ESP_LOGW(TAG,
                 "MQTT outbox enqueue failed (drops=%lu depth=%lu/%lu topic=%s)",
                 static_cast<unsigned long>(dropped),
                 static_cast<unsigned long>(uxQueueMessagesWaiting(mqtt_outbox)),
                 static_cast<unsigned long>(kMqttOutboxDepth), item.topic);
    }
    sample_queue_levels();
    return false;
}

static std::string derive_meter_key(const wmbus_minimal_pipeline::WmbusFrame& frame) {
    return frame.identity_key();
}

static void radio_rx_task(void* /*param*/) {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    auto& wd = watchdog_service::WatchdogService::instance();

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for radio_rx");
    }

    while (true) {
        radio_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        rsm.tick();

        auto result = radio.read_frame();
        if (result.is_ok() && frame_queue) {
            rsm.on_read_success();
            radio_read_success_count.fetch_add(1, std::memory_order_relaxed);
            radio_not_found_streak.store(0, std::memory_order_relaxed);
            auto frame = result.value();
            if (xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
                frame_enqueue_success.fetch_add(1, std::memory_order_relaxed);
                sample_queue_levels();
            } else {
                const uint32_t dropped =
                    frame_enqueue_drop.fetch_add(1, std::memory_order_relaxed) + 1;
                frame_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
                frame_queue_send_failures.fetch_add(1, std::memory_order_relaxed);
                if ((dropped % 32U) == 1U) {
                    ESP_LOGW(TAG, "Frame queue full/drop (drops=%lu depth=%lu/%lu)",
                             static_cast<unsigned long>(dropped),
                             static_cast<unsigned long>(uxQueueMessagesWaiting(frame_queue)),
                             static_cast<unsigned long>(kFrameQueueDepth));
                }
                sample_queue_levels();
            }
        } else if (result.is_error()) {
            const auto err = result.error();
            if (err == common::ErrorCode::NotFound) {
                radio_read_not_found_count.fetch_add(1, std::memory_order_relaxed);
                const uint32_t streak =
                    radio_not_found_streak.fetch_add(1, std::memory_order_relaxed) + 1U;
                update_peak(radio_not_found_streak_peak, streak);
            } else {
                radio_not_found_streak.store(0, std::memory_order_relaxed);
                if (err == common::ErrorCode::Timeout) {
                    radio_read_timeout_count.fetch_add(1, std::memory_order_relaxed);
                }
                radio_read_error_count.fetch_add(1, std::memory_order_relaxed);
            }
            rsm.on_read_failure(result.error());
        }

        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in radio_rx (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void pipeline_task(void* /*param*/) {
    auto& router = telegram_router::TelegramRouter::instance();
    auto& wd = watchdog_service::WatchdogService::instance();
    uint32_t rx_count = 0;

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for pipeline");
    }

    radio_cc1101::RawRadioFrame raw{};
    while (true) {
        pipeline_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        if (frame_queue && xQueueReceive(frame_queue, &raw, pdMS_TO_TICKS(100)) == pdTRUE) {
            sample_queue_levels();
            rx_count++;
            pipeline_frames_processed.fetch_add(1, std::memory_order_relaxed);

            auto& ntp = ntp_service::NtpService::instance();
            const bool ntp_synced = ntp.status().synchronized;
            int64_t ts = ntp.now_epoch_ms();
            if (ts <= 0) {
                ts = ntp.monotonic_now_ms();
            }

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
                char ts_str[40] = "timestamp_unavailable";
                if (ntp_synced && ts > 0) {
                    const time_t sec = static_cast<time_t>(ts / 1000);
                    struct tm t;
                    gmtime_r(&sec, &t);
                    strftime(ts_str, sizeof(ts_str), "%Y-%m-%dT%H:%M:%SZ", &t);
                } else if (ts > 0) {
                    std::snprintf(ts_str, sizeof(ts_str), "monotonic_ms:%lld",
                                  static_cast<long long>(ts));
                }

                enqueue_mqtt(mqtt_service::topic_raw_frame(cfg.mqtt.prefix, cfg.device.hostname),
                             mqtt_service::payload_raw_frame(
                                 frame.raw_hex().c_str(), frame.metadata.frame_length,
                                 frame.metadata.rssi_dbm, frame.metadata.lqi, frame.metadata.crc_ok,
                                 frame.manufacturer_id(), frame.device_id(),
                                 derive_meter_key(frame).c_str(), ts_str, rx_count));
            }

            if (route.publish_event && route.event_message) {
                enqueue_mqtt(
                    mqtt_service::topic_events(cfg.mqtt.prefix, cfg.device.hostname),
                    mqtt_service::payload_event("radio_event", "warning", route.event_message, ""));
            }
        }
        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in pipeline (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }
    }
}

static void mqtt_task(void* /*param*/) {
    auto& mqtt = mqtt_service::MqttService::instance();
    auto& wd = watchdog_service::WatchdogService::instance();
    MqttOutboxItem item{};

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for mqtt_pub");
    }

    while (true) {
        mqtt_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        if (mqtt_outbox && xQueueReceive(mqtt_outbox, &item, pdMS_TO_TICKS(500)) == pdTRUE) {
            sample_queue_levels();
            if (mqtt.is_connected()) {
                mqtt.publish(item.topic, item.payload, 0, false);
            } else {
                mqtt.report_outbox_dropped_disconnected();
            }
        }
        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in mqtt_pub (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void health_task(void* /*param*/) {
    // Low-frequency telemetry (30s cadence) must not subscribe to TWDT: default timeout is 5s.
    bool radio_stalled = false;
    bool pipeline_stalled = false;
    bool mqtt_stalled = false;

    while (true) {
        auto& wifi = wifi_manager::WifiManager::instance();
        auto& mqtt = mqtt_service::MqttService::instance();
        auto& health = health_monitor::HealthMonitor::instance();
        const uint32_t now = now_ms();

        const bool radio_now_stalled =
            (now - radio_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;
        const bool pipeline_now_stalled =
            (now - pipeline_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;
        const bool mqtt_now_stalled =
            (now - mqtt_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;

        if (radio_now_stalled && !radio_stalled) {
            radio_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("radio_rx task heartbeat stalled");
        }
        if (pipeline_now_stalled && !pipeline_stalled) {
            pipeline_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("pipeline task heartbeat stalled");
        }
        if (mqtt_now_stalled && !mqtt_stalled) {
            mqtt_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("mqtt task heartbeat stalled");
        }
        radio_stalled = radio_now_stalled;
        pipeline_stalled = pipeline_now_stalled;
        mqtt_stalled = mqtt_now_stalled;

        const bool any_critical_stalled = radio_stalled || pipeline_stalled || mqtt_stalled;
        if (!any_critical_stalled && wifi.state() == wifi_manager::WifiState::Connected &&
            mqtt.is_connected()) {
            health.report_healthy();
        } else if (wifi.state() == wifi_manager::WifiState::Disconnected) {
            health.report_warning("WiFi disconnected");
        } else if (!any_critical_stalled && !mqtt.is_connected()) {
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
        sample_queue_levels();
        sample_task_stack_watermarks();

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

} // namespace

namespace app_core {

common::Result<void> AppCore::create_runtime_tasks() {
    frame_enqueue_success.store(0, std::memory_order_relaxed);
    frame_enqueue_drop.store(0, std::memory_order_relaxed);
    frame_enqueue_errors.store(0, std::memory_order_relaxed);
    frame_queue_peak_depth.store(0, std::memory_order_relaxed);
    frame_queue_max_depth.store(0, std::memory_order_relaxed);
    frame_queue_send_failures.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_success.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_drop.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_errors.store(0, std::memory_order_relaxed);
    mqtt_outbox_peak_depth.store(0, std::memory_order_relaxed);
    const uint32_t now = now_ms();
    radio_loop_last_ms.store(now, std::memory_order_relaxed);
    pipeline_loop_last_ms.store(now, std::memory_order_relaxed);
    mqtt_loop_last_ms.store(now, std::memory_order_relaxed);
    pipeline_frames_processed.store(0, std::memory_order_relaxed);
    radio_read_success_count.store(0, std::memory_order_relaxed);
    radio_read_not_found_count.store(0, std::memory_order_relaxed);
    radio_read_timeout_count.store(0, std::memory_order_relaxed);
    radio_read_error_count.store(0, std::memory_order_relaxed);
    radio_not_found_streak.store(0, std::memory_order_relaxed);
    radio_not_found_streak_peak.store(0, std::memory_order_relaxed);
    radio_stall_count.store(0, std::memory_order_relaxed);
    pipeline_stall_count.store(0, std::memory_order_relaxed);
    mqtt_stall_count.store(0, std::memory_order_relaxed);
    watchdog_register_errors.store(0, std::memory_order_relaxed);
    watchdog_feed_errors.store(0, std::memory_order_relaxed);
    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();

    frame_queue = xQueueCreate(kFrameQueueDepth, sizeof(radio_cc1101::RawRadioFrame));
    mqtt_outbox = xQueueCreate(kMqttOutboxDepth, sizeof(MqttOutboxItem));
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

    if (xTaskCreatePinnedToCore(radio_rx_task, "radio_rx", 4096, nullptr, 10, &radio_task_handle,
                                1) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: radio_rx");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(pipeline_task, "pipeline", 4096, nullptr, 7, &pipeline_task_handle,
                                0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: pipeline");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(mqtt_task, "mqtt_pub", 6144, nullptr, 5, &mqtt_task_handle, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: mqtt_pub");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(health_task, "health", 4096, nullptr, 3, &health_task_handle, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: health");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    ESP_LOGI(
        TAG,
        "Runtime tasks created (radio_rx@Core1, pipeline@Core0, mqtt_pub@Core0, health@Core0)");
    sample_queue_levels();
    sample_task_stack_watermarks();
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
