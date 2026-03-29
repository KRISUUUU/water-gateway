#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ntp_service/ntp_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include <memory>
#include <string>

#include "cJSON.h"

#ifndef HOST_TEST_BUILD
#include "esp_system.h"
#endif

namespace diagnostics_service {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

const char* radio_state_str(radio_cc1101::RadioState s) {
    using radio_cc1101::RadioState;
    switch (s) {
    case RadioState::Uninitialized:
        return "Uninitialized";
    case RadioState::Idle:
        return "Idle";
    case RadioState::Receiving:
        return "Receiving";
    case RadioState::Error:
        return "Error";
    }
    return "Unknown";
}

const char* mqtt_state_str(mqtt_service::MqttState s) {
    using mqtt_service::MqttState;
    switch (s) {
    case MqttState::Uninitialized:
        return "Uninitialized";
    case MqttState::Disconnected:
        return "Disconnected";
    case MqttState::Connecting:
        return "Connecting";
    case MqttState::Connected:
        return "Connected";
    case MqttState::Error:
        return "Error";
    }
    return "Unknown";
}

const char* wifi_state_str(wifi_manager::WifiState s) {
    using wifi_manager::WifiState;
    switch (s) {
    case WifiState::Uninitialized:
        return "Uninitialized";
    case WifiState::Disconnected:
        return "Disconnected";
    case WifiState::Connecting:
        return "Connecting";
    case WifiState::Connected:
        return "Connected";
    case WifiState::ApMode:
        return "ApMode";
    }
    return "Unknown";
}

std::string to_unformatted_json(cJSON* root) {
    if (!root) {
        return "{}";
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return "{}";
    }
    return std::string(printed.get());
}

void fill_radio(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON_AddStringToObject(root, "radio_state", radio_state_str(snap.radio_state));
    cJSON* counters = cJSON_AddObjectToObject(root, "radio_counters");
    if (!counters) {
        return;
    }
    cJSON_AddNumberToObject(counters, "frames_received",
                            static_cast<double>(snap.radio.frames_received));
    cJSON_AddNumberToObject(counters, "rx_read_calls",
                            static_cast<double>(snap.radio.rx_read_calls));
    cJSON_AddNumberToObject(counters, "rx_not_found",
                            static_cast<double>(snap.radio.rx_not_found));
    cJSON_AddNumberToObject(counters, "rx_timeouts",
                            static_cast<double>(snap.radio.rx_timeouts));
    cJSON_AddNumberToObject(counters, "frames_crc_ok",
                            static_cast<double>(snap.radio.frames_crc_ok));
    cJSON_AddNumberToObject(counters, "frames_crc_fail",
                            static_cast<double>(snap.radio.frames_crc_fail));
    cJSON_AddNumberToObject(counters, "frames_incomplete",
                            static_cast<double>(snap.radio.frames_incomplete));
    cJSON_AddNumberToObject(counters, "frames_dropped_too_long",
                            static_cast<double>(snap.radio.frames_dropped_too_long));
    cJSON_AddNumberToObject(counters, "fifo_overflows",
                            static_cast<double>(snap.radio.fifo_overflows));
    cJSON_AddNumberToObject(counters, "radio_resets", static_cast<double>(snap.radio.radio_resets));
    cJSON_AddNumberToObject(counters, "radio_recoveries",
                            static_cast<double>(snap.radio.radio_recoveries));
    cJSON_AddNumberToObject(counters, "spi_errors", static_cast<double>(snap.radio.spi_errors));
}

void fill_mqtt(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* mqtt = cJSON_AddObjectToObject(root, "mqtt");
    if (!mqtt) {
        return;
    }
    cJSON_AddStringToObject(mqtt, "state", mqtt_state_str(snap.mqtt.state));
    cJSON_AddNumberToObject(mqtt, "publish_count", static_cast<double>(snap.mqtt.publish_count));
    cJSON_AddNumberToObject(mqtt, "publish_failures",
                            static_cast<double>(snap.mqtt.publish_failures));
    cJSON_AddNumberToObject(mqtt, "reconnect_count",
                            static_cast<double>(snap.mqtt.reconnect_count));
    cJSON_AddNumberToObject(mqtt, "last_publish_epoch_ms",
                            static_cast<double>(snap.mqtt.last_publish_epoch_ms));
    cJSON_AddStringToObject(mqtt, "broker_uri", snap.mqtt.broker_uri);
}

void fill_wifi(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    if (!wifi) {
        return;
    }
    cJSON_AddStringToObject(wifi, "state", wifi_state_str(snap.wifi.state));
    cJSON_AddNumberToObject(wifi, "rssi_dbm", static_cast<double>(snap.wifi.rssi_dbm));
    cJSON_AddNumberToObject(wifi, "reconnect_count",
                            static_cast<double>(snap.wifi.reconnect_count));
    cJSON_AddStringToObject(wifi, "ip_address", snap.wifi.ip_address);
    cJSON_AddStringToObject(wifi, "ssid", snap.wifi.ssid);
}

void fill_metrics(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* metrics = cJSON_AddObjectToObject(root, "metrics");
    if (!metrics) {
        return;
    }
    cJSON_AddNumberToObject(metrics, "uptime_s", static_cast<double>(snap.metrics.uptime_s));
    cJSON_AddNumberToObject(metrics, "free_heap_bytes",
                            static_cast<double>(snap.metrics.free_heap_bytes));
    cJSON_AddNumberToObject(metrics, "min_free_heap_bytes",
                            static_cast<double>(snap.metrics.min_free_heap_bytes));
    cJSON_AddNumberToObject(metrics, "largest_free_block",
                            static_cast<double>(snap.metrics.largest_free_block));
    cJSON_AddNumberToObject(metrics, "free_internal_heap_bytes",
                            static_cast<double>(snap.metrics.free_internal_heap_bytes));
    cJSON_AddNumberToObject(metrics, "min_internal_heap_bytes",
                            static_cast<double>(snap.metrics.min_internal_heap_bytes));
    cJSON_AddNumberToObject(metrics, "reset_reason_code",
                            static_cast<double>(snap.metrics.reset_reason_code));
#ifndef HOST_TEST_BUILD
    cJSON_AddStringToObject(metrics, "reset_reason",
                            esp_reset_reason_to_name(
                                static_cast<esp_reset_reason_t>(snap.metrics.reset_reason_code)));
#else
    cJSON_AddStringToObject(metrics, "reset_reason", "host_build");
#endif

    cJSON* tasks = cJSON_AddObjectToObject(metrics, "tasks");
    if (!tasks) {
        return;
    }
    cJSON_AddNumberToObject(tasks, "radio_loop_age_ms",
                            static_cast<double>(snap.metrics.tasks.radio_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "pipeline_loop_age_ms",
                            static_cast<double>(snap.metrics.tasks.pipeline_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "mqtt_loop_age_ms",
                            static_cast<double>(snap.metrics.tasks.mqtt_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "pipeline_frames_processed",
                            static_cast<double>(snap.metrics.tasks.pipeline_frames_processed));
    cJSON_AddNumberToObject(tasks, "radio_read_success_count",
                            static_cast<double>(snap.metrics.tasks.radio_read_success_count));
    cJSON_AddNumberToObject(tasks, "radio_read_not_found_count",
                            static_cast<double>(snap.metrics.tasks.radio_read_not_found_count));
    cJSON_AddNumberToObject(tasks, "radio_read_timeout_count",
                            static_cast<double>(snap.metrics.tasks.radio_read_timeout_count));
    cJSON_AddNumberToObject(tasks, "radio_read_error_count",
                            static_cast<double>(snap.metrics.tasks.radio_read_error_count));
    cJSON_AddNumberToObject(tasks, "radio_not_found_streak",
                            static_cast<double>(snap.metrics.tasks.radio_not_found_streak));
    cJSON_AddNumberToObject(tasks, "radio_not_found_streak_peak",
                            static_cast<double>(snap.metrics.tasks.radio_not_found_streak_peak));
    cJSON_AddNumberToObject(tasks, "radio_stall_count",
                            static_cast<double>(snap.metrics.tasks.radio_stall_count));
    cJSON_AddNumberToObject(tasks, "pipeline_stall_count",
                            static_cast<double>(snap.metrics.tasks.pipeline_stall_count));
    cJSON_AddNumberToObject(tasks, "mqtt_stall_count",
                            static_cast<double>(snap.metrics.tasks.mqtt_stall_count));
    cJSON_AddNumberToObject(tasks, "watchdog_register_errors",
                            static_cast<double>(snap.metrics.tasks.watchdog_register_errors));
    cJSON_AddNumberToObject(tasks, "watchdog_feed_errors",
                            static_cast<double>(snap.metrics.tasks.watchdog_feed_errors));
    cJSON_AddNumberToObject(tasks, "radio_stack_hwm_words",
                            static_cast<double>(snap.metrics.tasks.radio_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "pipeline_stack_hwm_words",
                            static_cast<double>(snap.metrics.tasks.pipeline_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "mqtt_stack_hwm_words",
                            static_cast<double>(snap.metrics.tasks.mqtt_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "health_stack_hwm_words",
                            static_cast<double>(snap.metrics.tasks.health_stack_hwm_words));
}

void fill_health(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* health = cJSON_AddObjectToObject(root, "health");
    if (!health) {
        return;
    }
    cJSON_AddStringToObject(health, "state",
                            health_monitor::HealthMonitor::state_to_string(snap.health.state));
    cJSON_AddNumberToObject(health, "warning_count",
                            static_cast<double>(snap.health.warning_count));
    cJSON_AddNumberToObject(health, "error_count", static_cast<double>(snap.health.error_count));
    cJSON_AddNumberToObject(health, "uptime_s", static_cast<double>(snap.health.uptime_s));
    cJSON_AddNumberToObject(health, "last_transition_uptime_s",
                            static_cast<double>(snap.health.last_transition_uptime_s));
    cJSON_AddNumberToObject(health, "last_warning_uptime_s",
                            static_cast<double>(snap.health.last_warning_uptime_s));
    cJSON_AddNumberToObject(health, "last_error_uptime_s",
                            static_cast<double>(snap.health.last_error_uptime_s));
    cJSON_AddStringToObject(health, "last_warning_msg", snap.health.last_warning_msg.c_str());
    cJSON_AddStringToObject(health, "last_error_msg", snap.health.last_error_msg.c_str());
}

void fill_time(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* time = cJSON_AddObjectToObject(root, "time");
    if (!time) {
        return;
    }
    cJSON_AddBoolToObject(time, "ntp_synchronized", snap.ntp.synchronized);
    cJSON_AddNumberToObject(time, "last_ntp_sync_epoch_s",
                            static_cast<double>(snap.ntp.last_sync_epoch_s));
    cJSON_AddNumberToObject(time, "now_epoch_ms", static_cast<double>(snap.now_epoch_ms));
    cJSON_AddNumberToObject(time, "monotonic_ms", static_cast<double>(snap.monotonic_ms));
    cJSON_AddBoolToObject(time, "timestamp_uses_monotonic_fallback",
                          snap.timestamp_uses_monotonic_fallback);
    cJSON_AddStringToObject(time, "timestamp_source",
                            snap.timestamp_uses_monotonic_fallback ? "monotonic" : "epoch");
}

void fill_queues(cJSON* root, const DiagnosticsSnapshot& snap) {
    cJSON* q = cJSON_AddObjectToObject(root, "queues");
    if (!q) {
        return;
    }

    cJSON* frame = cJSON_AddObjectToObject(q, "frame_queue");
    if (frame) {
        cJSON_AddNumberToObject(frame, "depth",
                                static_cast<double>(snap.metrics.queues.frame_queue_depth));
        cJSON_AddNumberToObject(frame, "peak_depth",
                                static_cast<double>(snap.metrics.queues.frame_queue_peak_depth));
        cJSON_AddNumberToObject(frame, "enqueue_success",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_success));
        cJSON_AddNumberToObject(frame, "enqueue_drop",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_drop));
        cJSON_AddNumberToObject(frame, "enqueue_errors",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_errors));
    }

    cJSON* outbox = cJSON_AddObjectToObject(q, "mqtt_outbox");
    if (outbox) {
        cJSON_AddNumberToObject(outbox, "depth",
                                static_cast<double>(snap.metrics.queues.mqtt_outbox_depth));
        cJSON_AddNumberToObject(outbox, "peak_depth",
                                static_cast<double>(snap.metrics.queues.mqtt_outbox_peak_depth));
        cJSON_AddNumberToObject(outbox, "enqueue_success",
                                static_cast<double>(snap.metrics.queues.mqtt_outbox_enqueue_success));
        cJSON_AddNumberToObject(outbox, "enqueue_drop",
                                static_cast<double>(snap.metrics.queues.mqtt_outbox_enqueue_drop));
        cJSON_AddNumberToObject(outbox, "enqueue_errors",
                                static_cast<double>(snap.metrics.queues.mqtt_outbox_enqueue_errors));
    }
}

} // namespace

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

common::Result<DiagnosticsSnapshot> DiagnosticsService::snapshot() const {
    DiagnosticsSnapshot s{};
    s.radio_state = radio_cc1101::RadioCc1101::instance().state();
    s.radio = radio_cc1101::RadioCc1101::instance().counters();
    s.mqtt = mqtt_service::MqttService::instance().status();
    s.wifi = wifi_manager::WifiManager::instance().status();
    s.ntp = ntp_service::NtpService::instance().status();
    s.now_epoch_ms = ntp_service::NtpService::instance().now_epoch_ms();
    s.monotonic_ms = ntp_service::NtpService::instance().monotonic_now_ms();
    s.timestamp_uses_monotonic_fallback = (s.now_epoch_ms <= 0);

    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) {
        return common::Result<DiagnosticsSnapshot>::error(metrics_res.error());
    }
    s.metrics = metrics_res.value();

    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) {
        return common::Result<DiagnosticsSnapshot>::error(health_res.error());
    }
    s.health = health_res.value();
    return common::Result<DiagnosticsSnapshot>::ok(s);
}

std::string DiagnosticsService::to_json(const DiagnosticsSnapshot& snap) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    fill_radio(root.get(), snap);
    fill_mqtt(root.get(), snap);
    fill_wifi(root.get(), snap);
    fill_metrics(root.get(), snap);
    fill_health(root.get(), snap);
    fill_queues(root.get(), snap);
    fill_time(root.get(), snap);
    return to_unformatted_json(root.get());
}

} // namespace diagnostics_service
