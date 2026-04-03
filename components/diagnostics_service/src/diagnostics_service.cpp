#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ntp_service/ntp_service.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include <algorithm>
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

#ifndef HOST_TEST_BUILD
const char* reset_reason_str(unsigned int code) {
    switch (static_cast<esp_reset_reason_t>(code)) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "other_watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
#ifdef ESP_RST_SDIO
    case ESP_RST_SDIO:
        return "sdio";
#endif
    default:
        return "unknown";
    }
}
#endif

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

const char* radio_drop_reason_str(radio_cc1101::RadioDropReason reason) {
    using radio_cc1101::RadioDropReason;
    switch (reason) {
    case RadioDropReason::None:
        return "none";
    case RadioDropReason::OversizedBurst:
        return "oversized_burst";
    case RadioDropReason::BurstTimeout:
        return "burst_timeout";
    }
    return "unknown";
}

std::string drop_prefix_hex(const radio_cc1101::RadioDropInfo& drop) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(static_cast<size_t>(drop.prefix_length) * 2U);
    for (uint8_t i = 0; i < drop.prefix_length; ++i) {
        const uint8_t b = drop.prefix[i];
        out.push_back(hex_chars[(b >> 4U) & 0x0F]);
        out.push_back(hex_chars[b & 0x0F]);
    }
    return out;
}

bool same_radio_drop(const radio_cc1101::RadioDropInfo& lhs, const radio_cc1101::RadioDropInfo& rhs) {
    if (lhs.reason != rhs.reason || lhs.captured_length != rhs.captured_length ||
        lhs.elapsed_ms != rhs.elapsed_ms || lhs.first_data_byte != rhs.first_data_byte ||
        lhs.prefix_length != rhs.prefix_length || lhs.quality_issue != rhs.quality_issue) {
        return false;
    }

    for (uint8_t i = 0; i < lhs.prefix_length; ++i) {
        if (lhs.prefix[i] != rhs.prefix[i]) {
            return false;
        }
    }
    return true;
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
    cJSON_AddStringToObject(root, "radio_rx_mode",
                            snap.radio_rx_polling_mode ? "polling" : "session_engine");
    cJSON_AddBoolToObject(root, "radio_rx_interrupt_path_active",
                          snap.radio_rx_interrupt_path_active);
    cJSON_AddStringToObject(root, "radio_rx_hardware_validation",
                            "required_for_burst_load_fifo_timeout_timing");
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
    cJSON_AddNumberToObject(counters, "recovery_attempts",
                            static_cast<double>(snap.radio_recovery_attempts));
    cJSON_AddNumberToObject(counters, "recovery_failures",
                            static_cast<double>(snap.radio_recovery_failures));
    cJSON_AddNumberToObject(counters, "soft_failure_streak",
                            static_cast<double>(snap.radio_soft_failure_streak));
    cJSON_AddNumberToObject(counters, "consecutive_errors",
                            static_cast<double>(snap.radio_consecutive_errors));
    cJSON_AddNumberToObject(counters, "last_recovery_reason_code",
                            static_cast<double>(snap.radio_last_recovery_reason_code));
    cJSON_AddStringToObject(counters, "last_recovery_reason",
                            common::error_code_to_string(static_cast<common::ErrorCode>(
                                snap.radio_last_recovery_reason_code)));
    cJSON* last_drop = cJSON_AddObjectToObject(counters, "last_drop");
    if (!last_drop) {
        return;
    }
    cJSON_AddStringToObject(last_drop, "reason", radio_drop_reason_str(snap.radio_last_drop.reason));
    cJSON_AddBoolToObject(last_drop, "quality_issue", snap.radio_last_drop.quality_issue);
    cJSON_AddNumberToObject(last_drop, "captured_length",
                            static_cast<double>(snap.radio_last_drop.captured_length));
    cJSON_AddNumberToObject(last_drop, "first_data_byte",
                            static_cast<double>(snap.radio_last_drop.first_data_byte));
    cJSON_AddStringToObject(last_drop, "prefix_hex", drop_prefix_hex(snap.radio_last_drop).c_str());
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
    cJSON_AddNumberToObject(mqtt, "outbox_enqueue_failures",
                            static_cast<double>(snap.mqtt.outbox_enqueue_failures));
    cJSON_AddNumberToObject(mqtt, "outbox_oversize_rejections",
                            static_cast<double>(snap.mqtt.outbox_oversize_rejections));
    cJSON_AddNumberToObject(mqtt, "outbox_max_depth",
                            static_cast<double>(snap.mqtt.outbox_max_depth));
    cJSON_AddNumberToObject(mqtt, "outbox_dropped_disconnected",
                            static_cast<double>(snap.mqtt.outbox_dropped_disconnected));
    cJSON_AddNumberToObject(mqtt, "outbox_carry_pending",
                            static_cast<double>(snap.mqtt.outbox_carry_pending));
    cJSON_AddNumberToObject(mqtt, "outbox_carry_retry_attempts",
                            static_cast<double>(snap.mqtt.outbox_carry_retry_attempts));
    cJSON_AddNumberToObject(mqtt, "outbox_carry_drops",
                            static_cast<double>(snap.mqtt.outbox_carry_drops));
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
                            reset_reason_str(snap.metrics.reset_reason_code));
#else
    cJSON_AddStringToObject(metrics, "reset_reason", "host_build");
#endif

    cJSON* tasks = cJSON_AddObjectToObject(metrics, "tasks");
    if (!tasks) {
        return;
    }
    cJSON_AddNumberToObject(tasks, "radio_poll_delay_ms",
                            static_cast<double>(snap.metrics.tasks.radio_poll_delay_ms));
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
    cJSON_AddNumberToObject(tasks, "radio_poll_iterations",
                            static_cast<double>(snap.metrics.tasks.radio_poll_iterations));
    cJSON_AddNumberToObject(tasks, "radio_timeout_streak",
                            static_cast<double>(snap.metrics.tasks.radio_timeout_streak));
    cJSON_AddNumberToObject(tasks, "radio_timeout_streak_peak",
                            static_cast<double>(snap.metrics.tasks.radio_timeout_streak_peak));
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
        // `peak_depth` is retained for backward compatibility and mirrors `frame_queue_max_depth`.
        cJSON_AddNumberToObject(frame, "peak_depth",
                                static_cast<double>(snap.metrics.queues.frame_queue_peak_depth));
        cJSON_AddNumberToObject(frame, "enqueue_success",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_success));
        cJSON_AddNumberToObject(frame, "enqueue_drop",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_drop));
        cJSON_AddNumberToObject(frame, "enqueue_errors",
                                static_cast<double>(snap.metrics.queues.frame_enqueue_errors));
        cJSON_AddNumberToObject(frame, "frame_queue_max_depth",
                                static_cast<double>(snap.metrics.queues.frame_queue_max_depth));
        cJSON_AddNumberToObject(frame, "frame_queue_send_failures",
                                static_cast<double>(snap.metrics.queues.frame_queue_send_failures));
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
        cJSON_AddNumberToObject(outbox, "outbox_enqueue_failures",
                                static_cast<double>(snap.mqtt.outbox_enqueue_failures));
        cJSON_AddNumberToObject(outbox, "outbox_oversize_rejections",
                                static_cast<double>(snap.mqtt.outbox_oversize_rejections));
        cJSON_AddNumberToObject(outbox, "outbox_max_depth",
                                static_cast<double>(snap.mqtt.outbox_max_depth));
        cJSON_AddNumberToObject(outbox, "outbox_dropped_disconnected",
                                static_cast<double>(snap.mqtt.outbox_dropped_disconnected));
        cJSON_AddNumberToObject(outbox, "outbox_carry_pending",
                                static_cast<double>(snap.mqtt.outbox_carry_pending));
        cJSON_AddNumberToObject(outbox, "outbox_carry_retry_attempts",
                                static_cast<double>(snap.mqtt.outbox_carry_retry_attempts));
        cJSON_AddNumberToObject(outbox, "outbox_carry_drops",
                                static_cast<double>(snap.mqtt.outbox_carry_drops));
    }
}

void fill_rf_diagnostics(cJSON* root, const DiagnosticsSnapshot& snap) {
    JsonPtr rf(
        cJSON_Parse(rf_diagnostics::RfDiagnosticsService::to_json(snap.rf_diagnostics).c_str()),
        cJSON_Delete);
    if (!rf) {
        return;
    }
    cJSON_AddItemToObject(root, "rf_diagnostics", rf.release());
}

} // namespace

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

common::Result<DiagnosticsSnapshot> DiagnosticsService::snapshot() const {
    DiagnosticsSnapshot s{};
    const auto& rsm = radio_state_machine::RadioStateMachine::instance();
    s.radio_state = radio_cc1101::RadioCc1101::instance().state();
    s.radio = radio_cc1101::RadioCc1101::instance().counters();
    s.radio_last_drop = radio_cc1101::RadioCc1101::instance().last_drop();
    s.radio_rx_polling_mode = false;
    s.radio_rx_interrupt_path_active = true;
    s.radio_recovery_attempts = rsm.recovery_attempts();
    s.radio_recovery_failures = rsm.recovery_failures();
    s.radio_soft_failure_streak = rsm.soft_failure_streak();
    s.radio_consecutive_errors = rsm.consecutive_errors();
    s.radio_last_recovery_reason_code = static_cast<std::int32_t>(rsm.last_recovery_reason());
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
    ingest_radio_drop_if_new(s);
    s.rf_diagnostics = rf_diagnostics::RfDiagnosticsService::instance().snapshot();
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
    fill_rf_diagnostics(root.get(), snap);
    return to_unformatted_json(root.get());
}

void DiagnosticsService::ingest_radio_drop_if_new(const DiagnosticsSnapshot& snap) const {
    if (snap.radio_last_drop.reason == radio_cc1101::RadioDropReason::None) {
        return;
    }

    std::lock_guard<std::mutex> lock(rf_ingest_mutex_);
    if (last_ingested_radio_drop_valid_ &&
        same_radio_drop(last_ingested_radio_drop_, snap.radio_last_drop)) {
        return;
    }

    rf_diagnostics::RfDiagnosticRecord record{};
    record.timestamp_epoch_ms = snap.now_epoch_ms;
    record.monotonic_ms = snap.monotonic_ms;
    record.orientation = rf_diagnostics::Orientation::Unknown;
    record.expected_encoded_length = 0;
    record.actual_encoded_length = snap.radio_last_drop.captured_length;
    record.expected_decoded_length = 0;
    record.actual_decoded_length = 0;
    record.capture_elapsed_ms = snap.radio_last_drop.elapsed_ms;
    record.first_data_byte = snap.radio_last_drop.first_data_byte;
    record.quality_issue = snap.radio_last_drop.quality_issue;
    record.signal_quality_valid = false;
    record.captured_prefix_length = std::min<size_t>(snap.radio_last_drop.prefix_length,
                                                     record.captured_prefix.size());
    for (size_t i = 0; i < record.captured_prefix_length; ++i) {
        record.captured_prefix[i] = snap.radio_last_drop.prefix[i];
    }

    switch (snap.radio_last_drop.reason) {
    case radio_cc1101::RadioDropReason::OversizedBurst:
        record.reject_reason = rf_diagnostics::RejectReason::OversizedBurst;
        break;
    case radio_cc1101::RadioDropReason::BurstTimeout:
        record.reject_reason = rf_diagnostics::RejectReason::BurstTimeout;
        break;
    case radio_cc1101::RadioDropReason::None:
        record.reject_reason = rf_diagnostics::RejectReason::None;
        break;
    }

    const auto before = rf_diagnostics::RfDiagnosticsService::instance().snapshot();
    record.sequence = before.total_inserted + 1U;
    rf_diagnostics::RfDiagnosticsService::instance().insert(record);
    last_ingested_radio_drop_ = snap.radio_last_drop;
    last_ingested_radio_drop_valid_ = true;
}

} // namespace diagnostics_service
