#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "common/error.hpp"
#include "common/security_posture.hpp"
#include "config_store/config_store.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "ntp_service/ntp_service.hpp"
#include "protocol_driver/radio_profile_manager.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <memory>
#include <string>

namespace api_handlers::detail {

namespace {

const char* prios_control_mode(const config_store::RadioConfig& radio_cfg) {
    if (radio_cfg.prios_discovery_mode) {
        return "PRIOS discovery/sniffer override";
    }
    if (radio_cfg.prios_capture_campaign) {
        return "PRIOS campaign override";
    }
    return "Normal scheduler control";
}

const char* protocol_name_for_profile(protocol_driver::RadioProfileId profile_id) {
    switch (profile_id) {
        case protocol_driver::RadioProfileId::WMbusT868:
            return "WMBUS_T";
        case protocol_driver::RadioProfileId::WMbusPriosR3:
        case protocol_driver::RadioProfileId::WMbusPriosR4:
            return "PRIOS";
        default:
            return "Unknown";
    }
}

struct ProtocolRecentSummary {
    uint32_t recent_accepts = 0;
    int64_t last_success_timestamp_ms = 0;
    int8_t last_success_rssi_dbm = 0;
    uint8_t last_success_lqi = 0;
    const char* last_meter_key = "";
};

ProtocolRecentSummary summarize_recent_protocol(
    const std::vector<meter_registry::RecentTelegram>& recent,
    const char* protocol_name) {
    ProtocolRecentSummary summary{};
    for (const auto& telegram : recent) {
        if (telegram.protocol_name != protocol_name) {
            continue;
        }
        summary.recent_accepts++;
        if (summary.last_success_timestamp_ms == 0) {
            summary.last_success_timestamp_ms = telegram.timestamp_ms;
            summary.last_success_rssi_dbm = telegram.rssi_dbm;
            summary.last_success_lqi = telegram.lqi;
            summary.last_meter_key = telegram.meter_key.c_str();
        }
    }
    return summary;
}

ProtocolRecentSummary summarize_recent_prios(
    const std::vector<meter_registry::RecentTelegram>& recent) {
    ProtocolRecentSummary summary{};
    for (const auto& telegram : recent) {
        if (telegram.protocol_name != "PRIOS_R3" &&
            telegram.protocol_name != "PRIOS_R4" &&
            telegram.protocol_name != "PRIOS") {
            continue;
        }
        summary.recent_accepts++;
        if (summary.last_success_timestamp_ms == 0) {
            summary.last_success_timestamp_ms = telegram.timestamp_ms;
            summary.last_success_rssi_dbm = telegram.rssi_dbm;
            summary.last_success_lqi = telegram.lqi;
            summary.last_meter_key = telegram.meter_key.c_str();
        }
    }
    return summary;
}

void add_protocol_runtime_json(cJSON* root) {
    const auto sched = protocol_driver::RadioProfileManager::instance().status();
    const auto cfg = config_store::ConfigStore::instance().config();
    const auto recent =
        meter_registry::MeterRegistry::instance().recent_telegrams(
            meter_registry::TelegramFilter::All);
    const auto tmode_recent = summarize_recent_protocol(recent, "WMBUS_T");
    const auto prios_recent = summarize_recent_prios(recent);
    auto rf_snapshot = rf_diagnostics::RfDiagnosticsService::instance().snapshot_allocated();
    const auto prios_stats = wmbus_prios_rx::PriosCaptureService::instance().stats();

    std::string last_tmode_reject = "none";
    if (rf_snapshot && rf_snapshot->count > 0) {
        last_tmode_reject = rf_diagnostics::RfDiagnosticsService::reject_reason_to_string(
            rf_snapshot->records[rf_snapshot->count - 1].reject_reason);
    }

    cJSON* runtime = cJSON_AddObjectToObject(root, "radio_runtime");
    cJSON_AddStringToObject(runtime, "active_profile",
                            protocol_driver::radio_profile_id_to_string(sched.active_profile_id));
    cJSON_AddStringToObject(runtime, "active_protocol",
                            protocol_name_for_profile(sched.active_profile_id));
    cJSON_AddStringToObject(runtime, "control_mode",
                            prios_control_mode(cfg.radio));
    cJSON_AddBoolToObject(runtime, "prios_override_active",
                          cfg.radio.prios_capture_campaign || cfg.radio.prios_discovery_mode);
    cJSON_AddStringToObject(runtime, "configured_prios_profile",
                            protocol_driver::radio_profile_id_to_string(cfg.radio.prios_profile));
    cJSON_AddStringToObject(runtime, "last_wake_source",
                            protocol_driver::runtime_wake_source_to_string(
                                sched.last_wake_source));

    cJSON* tmode = cJSON_AddObjectToObject(runtime, "tmode");
    cJSON_AddNumberToObject(tmode, "recent_accepts",
                            static_cast<double>(tmode_recent.recent_accepts));
    cJSON_AddStringToObject(tmode, "last_reject_reason", last_tmode_reject.c_str());
    cJSON_AddNumberToObject(tmode, "last_success_timestamp_ms",
                            static_cast<double>(tmode_recent.last_success_timestamp_ms));
    cJSON_AddStringToObject(tmode, "last_success_meter_key", tmode_recent.last_meter_key);
    cJSON_AddNumberToObject(tmode, "last_success_rssi_dbm",
                            static_cast<double>(tmode_recent.last_success_rssi_dbm));
    cJSON_AddNumberToObject(tmode, "last_success_lqi",
                            static_cast<double>(tmode_recent.last_success_lqi));

    cJSON* prios = cJSON_AddObjectToObject(runtime, "prios");
    cJSON_AddStringToObject(prios, "support_level", "identity_only_capture");
    cJSON_AddBoolToObject(prios, "reading_decode_available", false);
    cJSON_AddNumberToObject(prios, "recent_accepts",
                            static_cast<double>(prios_recent.recent_accepts));
    cJSON_AddStringToObject(prios, "last_reject_reason", prios_stats.last_reject_reason);
    cJSON_AddNumberToObject(prios, "last_success_timestamp_ms",
                            static_cast<double>(prios_recent.last_success_timestamp_ms));
    cJSON_AddStringToObject(prios, "last_success_meter_key", prios_recent.last_meter_key);
    cJSON_AddNumberToObject(prios, "last_success_rssi_dbm",
                            static_cast<double>(prios_recent.last_success_rssi_dbm));
    cJSON_AddNumberToObject(prios, "last_success_lqi",
                            static_cast<double>(prios_recent.last_success_lqi));
}

bool serialize_json_object(cJSON* root, std::string& out) {
    if (!root) {
        return false;
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return false;
    }
    out.assign(printed.get());
    return true;
}

esp_err_t send_chunk(httpd_req_t* req, const std::string& chunk) {
    return httpd_resp_send_chunk(req, chunk.c_str(), static_cast<ssize_t>(chunk.size()));
}

esp_err_t send_chunk(httpd_req_t* req, const char* chunk) {
    return httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_json_fragment(httpd_req_t* req, const std::string& fragment, bool& first_field) {
    if (fragment.size() < 2 || fragment.front() != '{' || fragment.back() != '}') {
        return ESP_ERR_INVALID_ARG;
    }
    std::string chunk;
    if (!first_field) {
        chunk.push_back(',');
    }
    chunk.append(fragment, 1, fragment.size() - 2);
    first_field = false;
    return send_chunk(req, chunk);
}



template <typename Builder>
esp_err_t send_json_object_section(httpd_req_t* req, bool& first_field, Builder&& builder) {
    JsonPtr root = make_json_object();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    builder(root.get());
    std::string json;
    if (!serialize_json_object(root.get(), json)) {
        return ESP_ERR_NO_MEM;
    }
    return send_json_fragment(req, json, first_field);
}

void add_health_json(cJSON* root, const health_monitor::HealthSnapshot& health) {
    cJSON* obj = cJSON_AddObjectToObject(root, "health");
    cJSON_AddStringToObject(obj, "state", health_monitor::HealthMonitor::state_to_string(health.state));
    cJSON_AddNumberToObject(obj, "warning_count", static_cast<double>(health.warning_count));
    cJSON_AddNumberToObject(obj, "error_count", static_cast<double>(health.error_count));
    cJSON_AddNumberToObject(obj, "uptime_s", static_cast<double>(health.uptime_s));
    cJSON_AddStringToObject(obj, "last_warning_msg", health.last_warning_msg.c_str());
    cJSON_AddStringToObject(obj, "last_error_msg", health.last_error_msg.c_str());
    cJSON_AddNumberToObject(obj, "last_transition_uptime_s", static_cast<double>(health.last_transition_uptime_s));
    cJSON_AddNumberToObject(obj, "last_warning_uptime_s", static_cast<double>(health.last_warning_uptime_s));
    cJSON_AddNumberToObject(obj, "last_error_uptime_s", static_cast<double>(health.last_error_uptime_s));
}

void add_health_summary_json(cJSON* root, const health_monitor::HealthSnapshot& health) {
    cJSON* obj = cJSON_AddObjectToObject(root, "health");
    cJSON_AddStringToObject(obj, "state", health_monitor::HealthMonitor::state_to_string(health.state));
    cJSON_AddNumberToObject(obj, "warning_count", static_cast<double>(health.warning_count));
    cJSON_AddNumberToObject(obj, "error_count", static_cast<double>(health.error_count));
    cJSON_AddNumberToObject(obj, "uptime_s", static_cast<double>(health.uptime_s));
}

void add_metrics_json(cJSON* root, const metrics_service::RuntimeMetrics& metrics) {
    cJSON* obj = cJSON_AddObjectToObject(root, "metrics");
    cJSON_AddNumberToObject(obj, "uptime_s", static_cast<double>(metrics.uptime_s));
    cJSON_AddNumberToObject(obj, "free_heap_bytes", static_cast<double>(metrics.free_heap_bytes));
    cJSON_AddNumberToObject(obj, "min_free_heap_bytes", static_cast<double>(metrics.min_free_heap_bytes));
    cJSON_AddNumberToObject(obj, "largest_free_block", static_cast<double>(metrics.largest_free_block));
    cJSON_AddNumberToObject(obj, "free_internal_heap_bytes", static_cast<double>(metrics.free_internal_heap_bytes));
    cJSON_AddNumberToObject(obj, "min_internal_heap_bytes", static_cast<double>(metrics.min_internal_heap_bytes));
    cJSON_AddNumberToObject(obj, "reset_reason_code", static_cast<double>(metrics.reset_reason_code));
    cJSON_AddStringToObject(obj, "reset_reason", reset_reason_str(metrics.reset_reason_code));
    cJSON* tasks = cJSON_AddObjectToObject(obj, "tasks");
    cJSON_AddNumberToObject(tasks, "radio_loop_age_ms", static_cast<double>(metrics.tasks.radio_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "pipeline_loop_age_ms", static_cast<double>(metrics.tasks.pipeline_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "mqtt_loop_age_ms", static_cast<double>(metrics.tasks.mqtt_loop_age_ms));
    cJSON_AddNumberToObject(tasks, "pipeline_frames_processed", static_cast<double>(metrics.tasks.pipeline_frames_processed));
    cJSON_AddNumberToObject(tasks, "radio_stall_count", static_cast<double>(metrics.tasks.radio_stall_count));
    cJSON_AddNumberToObject(tasks, "pipeline_stall_count", static_cast<double>(metrics.tasks.pipeline_stall_count));
    cJSON_AddNumberToObject(tasks, "mqtt_stall_count", static_cast<double>(metrics.tasks.mqtt_stall_count));
    cJSON_AddNumberToObject(tasks, "watchdog_register_errors", static_cast<double>(metrics.tasks.watchdog_register_errors));
    cJSON_AddNumberToObject(tasks, "watchdog_feed_errors", static_cast<double>(metrics.tasks.watchdog_feed_errors));
    cJSON_AddNumberToObject(tasks, "radio_stack_hwm_words", static_cast<double>(metrics.tasks.radio_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "pipeline_stack_hwm_words", static_cast<double>(metrics.tasks.pipeline_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "mqtt_stack_hwm_words", static_cast<double>(metrics.tasks.mqtt_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "health_stack_hwm_words", static_cast<double>(metrics.tasks.health_stack_hwm_words));
    cJSON* sessions = cJSON_AddObjectToObject(obj, "sessions");
    cJSON_AddNumberToObject(sessions, "completed", static_cast<double>(metrics.sessions.completed));
    cJSON_AddNumberToObject(sessions, "validated", static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(sessions, "rejected", static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(sessions, "aborted", static_cast<double>(metrics.sessions.incomplete));
    cJSON_AddNumberToObject(sessions, "radio_crc_available",
                            static_cast<double>(metrics.sessions.radio_crc_available));
    cJSON_AddNumberToObject(sessions, "radio_crc_unavailable",
                            static_cast<double>(metrics.sessions.radio_crc_unavailable));
    cJSON_AddNumberToObject(sessions, "radio_crc_ok",
                            static_cast<double>(metrics.sessions.radio_crc_ok));
    cJSON_AddNumberToObject(sessions, "radio_crc_fail",
                            static_cast<double>(metrics.sessions.radio_crc_fail));
}

void add_metrics_summary_json(cJSON* root, const metrics_service::RuntimeMetrics& metrics) {
    cJSON* obj = cJSON_AddObjectToObject(root, "metrics");
    cJSON_AddNumberToObject(obj, "uptime_s", static_cast<double>(metrics.uptime_s));
    cJSON_AddNumberToObject(obj, "free_heap_bytes", static_cast<double>(metrics.free_heap_bytes));
    cJSON_AddNumberToObject(obj, "min_free_heap_bytes", static_cast<double>(metrics.min_free_heap_bytes));
    cJSON_AddStringToObject(obj, "reset_reason", reset_reason_str(metrics.reset_reason_code));
    cJSON* sessions = cJSON_AddObjectToObject(obj, "sessions");
    cJSON_AddNumberToObject(sessions, "completed", static_cast<double>(metrics.sessions.completed));
    cJSON_AddNumberToObject(sessions, "validated", static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(sessions, "rejected", static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(sessions, "aborted", static_cast<double>(metrics.sessions.incomplete));
}

void add_time_json(cJSON* root) {
    const auto ntp_status = ntp_service::NtpService::instance().status();
    const int64_t now_epoch_ms = ntp_service::NtpService::instance().now_epoch_ms();
    cJSON* time = cJSON_AddObjectToObject(root, "time");
    cJSON_AddBoolToObject(time, "ntp_synchronized", ntp_status.synchronized);
    cJSON_AddNumberToObject(time, "last_ntp_sync_epoch_s", static_cast<double>(ntp_status.last_sync_epoch_s));
    cJSON_AddNumberToObject(time, "now_epoch_ms", static_cast<double>(now_epoch_ms));
    cJSON_AddNumberToObject(time, "monotonic_ms", static_cast<double>(ntp_service::NtpService::instance().monotonic_now_ms()));
    cJSON_AddBoolToObject(time, "timestamp_uses_monotonic_fallback", now_epoch_ms <= 0);
    cJSON_AddStringToObject(time, "timestamp_source", now_epoch_ms > 0 ? "epoch" : "monotonic");
}

void add_time_summary_json(cJSON* root) {
    const auto ntp_status = ntp_service::NtpService::instance().status();
    const int64_t now_epoch_ms = ntp_service::NtpService::instance().now_epoch_ms();
    cJSON* time = cJSON_AddObjectToObject(root, "time");
    cJSON_AddBoolToObject(time, "ntp_synchronized", ntp_status.synchronized);
    cJSON_AddNumberToObject(time, "now_epoch_ms", static_cast<double>(now_epoch_ms));
}

void add_security_json(cJSON* root, const config_store::AppConfig& cfg) {
    const auto sec = common::build_security_posture();
    const uint32_t missing = static_cast<uint32_t>((!sec.secure_boot_enabled) + (!sec.flash_encryption_enabled) +
                                                   (!sec.nvs_encryption_enabled) + (!sec.anti_rollback_enabled) +
                                                   (!sec.ota_rollback_enabled));
    cJSON* security = cJSON_AddObjectToObject(root, "security");
    cJSON_AddBoolToObject(security, "admin_password_set", cfg.auth.has_password());
    cJSON_AddBoolToObject(security, "provisioning_ap_open", !cfg.wifi.is_configured());
    cJSON_AddBoolToObject(security, "bootstrap_login_open", !cfg.wifi.is_configured() && !cfg.auth.has_password());
    cJSON* build = cJSON_AddObjectToObject(security, "build");
    cJSON_AddBoolToObject(build, "secure_boot_enabled", sec.secure_boot_enabled);
    cJSON_AddBoolToObject(build, "flash_encryption_enabled", sec.flash_encryption_enabled);
    cJSON_AddBoolToObject(build, "nvs_encryption_enabled", sec.nvs_encryption_enabled);
    cJSON_AddBoolToObject(build, "anti_rollback_enabled", sec.anti_rollback_enabled);
    cJSON_AddBoolToObject(build, "ota_rollback_enabled", sec.ota_rollback_enabled);
    cJSON_AddBoolToObject(build, "production_hardening_ready", common::build_is_hardened_for_production());
    cJSON* missing_obj = cJSON_AddObjectToObject(build, "hardening_missing");
    cJSON_AddBoolToObject(missing_obj, "secure_boot", !sec.secure_boot_enabled);
    cJSON_AddBoolToObject(missing_obj, "flash_encryption", !sec.flash_encryption_enabled);
    cJSON_AddBoolToObject(missing_obj, "nvs_encryption", !sec.nvs_encryption_enabled);
    cJSON_AddBoolToObject(missing_obj, "anti_rollback", !sec.anti_rollback_enabled);
    cJSON_AddBoolToObject(missing_obj, "ota_rollback", !sec.ota_rollback_enabled);
    cJSON_AddNumberToObject(build, "hardening_missing_count", static_cast<double>(missing));
}

void add_config_runtime_json(cJSON* root, const config_store::ConfigRuntimeStatus& cfg_runtime) {
    cJSON* obj = cJSON_AddObjectToObject(root, "config_store");
    cJSON_AddStringToObject(obj, "load_source", config_load_source_name(cfg_runtime.load_source));
    cJSON_AddBoolToObject(obj, "used_defaults", cfg_runtime.used_defaults);
    cJSON_AddBoolToObject(obj, "loaded_from_backup", cfg_runtime.loaded_from_backup);
    cJSON_AddBoolToObject(obj, "defaults_persisted", cfg_runtime.defaults_persisted);
    cJSON_AddBoolToObject(obj, "defaults_persist_deferred", cfg_runtime.defaults_persist_deferred);
    cJSON_AddStringToObject(obj, "last_load_error", common::error_code_to_string(cfg_runtime.last_load_error));
    cJSON_AddStringToObject(obj, "last_persist_error", common::error_code_to_string(cfg_runtime.last_persist_error));
    cJSON_AddStringToObject(obj, "last_migration_error", common::error_code_to_string(cfg_runtime.last_migration_error));
    cJSON_AddNumberToObject(obj, "load_attempts", static_cast<double>(cfg_runtime.load_attempts));
    cJSON_AddNumberToObject(obj, "load_failures", static_cast<double>(cfg_runtime.load_failures));
    cJSON_AddNumberToObject(obj, "primary_read_failures", static_cast<double>(cfg_runtime.primary_read_failures));
    cJSON_AddNumberToObject(obj, "backup_read_failures", static_cast<double>(cfg_runtime.backup_read_failures));
    cJSON_AddNumberToObject(obj, "validation_failures", static_cast<double>(cfg_runtime.validation_failures));
    cJSON_AddNumberToObject(obj, "migration_attempts", static_cast<double>(cfg_runtime.migration_attempts));
    cJSON_AddNumberToObject(obj, "migration_failures", static_cast<double>(cfg_runtime.migration_failures));
    cJSON_AddNumberToObject(obj, "save_attempts", static_cast<double>(cfg_runtime.save_attempts));
    cJSON_AddNumberToObject(obj, "save_successes", static_cast<double>(cfg_runtime.save_successes));
    cJSON_AddNumberToObject(obj, "save_failures", static_cast<double>(cfg_runtime.save_failures));
    cJSON_AddNumberToObject(obj, "save_validation_rejects", static_cast<double>(cfg_runtime.save_validation_rejects));
}

void add_runtime_links_json(cJSON* root, const metrics_service::RuntimeMetrics& metrics,
                            const config_store::AppConfig& cfg, const wifi_manager::WifiStatus& wifi,
                            const mqtt_service::MqttStatus& mqtt, const radio_cc1101::RadioCc1101& radio,
                            const ota_manager::OtaStatus& ota) {
    const auto& counters = radio.counters();
    cJSON_AddStringToObject(root, "mode", cfg.wifi.is_configured() ? "normal" : "provisioning");
    cJSON_AddStringToObject(root, "firmware_version", ota.current_version);
    cJSON* wifi_obj = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi_obj, "state", wifi_state_name(wifi.state));
    cJSON_AddStringToObject(wifi_obj, "ip_address", wifi.ip_address);
    cJSON_AddNumberToObject(wifi_obj, "rssi_dbm", static_cast<double>(wifi.rssi_dbm));
    cJSON_AddStringToObject(wifi_obj, "ssid", wifi.ssid);
    cJSON_AddNumberToObject(wifi_obj, "reconnect_count", static_cast<double>(wifi.reconnect_count));
    cJSON* mqtt_obj = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(mqtt_obj, "state", mqtt_state_name(mqtt.state));
    cJSON_AddStringToObject(mqtt_obj, "broker_uri", mqtt.broker_uri);
    cJSON_AddNumberToObject(mqtt_obj, "publish_count", static_cast<double>(mqtt.publish_count));
    cJSON_AddNumberToObject(mqtt_obj, "publish_failures", static_cast<double>(mqtt.publish_failures));
    cJSON_AddNumberToObject(mqtt_obj, "reconnect_count", static_cast<double>(mqtt.reconnect_count));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_enqueue_failures", static_cast<double>(mqtt.outbox_enqueue_failures));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_oversize_rejections", static_cast<double>(mqtt.outbox_oversize_rejections));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_max_depth", static_cast<double>(mqtt.outbox_max_depth));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_dropped_disconnected", static_cast<double>(mqtt.outbox_dropped_disconnected));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_carry_pending", static_cast<double>(mqtt.outbox_carry_pending));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_carry_retry_attempts", static_cast<double>(mqtt.outbox_carry_retry_attempts));
    cJSON_AddNumberToObject(mqtt_obj, "outbox_carry_drops", static_cast<double>(mqtt.outbox_carry_drops));
    cJSON* radio_obj = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddStringToObject(radio_obj, "state", radio_state_name(radio.state()));
    cJSON_AddNumberToObject(radio_obj, "frames_received", static_cast<double>(metrics.sessions.completed));
    // frames_crc_ok / frames_crc_fail: backward-compatible JSON keys.
    // Now sourced from link-layer validation outcomes (software CRC/block check),
    // not radio hardware CRC (which is disabled in T-mode profile).
    cJSON_AddNumberToObject(radio_obj, "frames_crc_ok", static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(radio_obj, "frames_crc_fail", static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(radio_obj, "telegrams_validated",
                            static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(radio_obj, "telegrams_rejected",
                            static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(radio_obj, "sessions_aborted",
                            static_cast<double>(metrics.sessions.incomplete));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_available_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_available));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_unavailable_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_unavailable));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_ok_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_ok));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_fail_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_fail));
    cJSON_AddNumberToObject(radio_obj, "frames_incomplete", static_cast<double>(metrics.sessions.incomplete));
    cJSON_AddNumberToObject(radio_obj, "frames_dropped_too_long", static_cast<double>(metrics.sessions.dropped_too_long));
    cJSON_AddNumberToObject(radio_obj, "fifo_overflows", static_cast<double>(counters.fifo_overflows));
    cJSON* queues = cJSON_AddObjectToObject(root, "queues");
    cJSON* frame_q = cJSON_AddObjectToObject(queues, "frame_queue");
    cJSON_AddNumberToObject(frame_q, "depth", static_cast<double>(metrics.queues.frame_queue_depth));
    cJSON_AddNumberToObject(frame_q, "peak_depth", static_cast<double>(metrics.queues.frame_queue_peak_depth));
    cJSON_AddNumberToObject(frame_q, "enqueue_success", static_cast<double>(metrics.queues.frame_enqueue_success));
    cJSON_AddNumberToObject(frame_q, "enqueue_drop", static_cast<double>(metrics.queues.frame_enqueue_drop));
    cJSON_AddNumberToObject(frame_q, "enqueue_errors", static_cast<double>(metrics.queues.frame_enqueue_errors));
    cJSON_AddNumberToObject(frame_q, "frame_queue_max_depth", static_cast<double>(metrics.queues.frame_queue_max_depth));
    cJSON_AddNumberToObject(frame_q, "frame_queue_send_failures", static_cast<double>(metrics.queues.frame_queue_send_failures));
    cJSON* outbox = cJSON_AddObjectToObject(queues, "mqtt_outbox");
    cJSON_AddNumberToObject(outbox, "depth", static_cast<double>(metrics.queues.mqtt_outbox_depth));
    cJSON_AddNumberToObject(outbox, "peak_depth", static_cast<double>(metrics.queues.mqtt_outbox_peak_depth));
    cJSON_AddNumberToObject(outbox, "enqueue_success", static_cast<double>(metrics.queues.mqtt_outbox_enqueue_success));
    cJSON_AddNumberToObject(outbox, "enqueue_drop", static_cast<double>(metrics.queues.mqtt_outbox_enqueue_drop));
    cJSON_AddNumberToObject(outbox, "enqueue_errors", static_cast<double>(metrics.queues.mqtt_outbox_enqueue_errors));
    cJSON_AddNumberToObject(outbox, "outbox_enqueue_failures", static_cast<double>(mqtt.outbox_enqueue_failures));
    cJSON_AddNumberToObject(outbox, "outbox_oversize_rejections", static_cast<double>(mqtt.outbox_oversize_rejections));
    cJSON_AddNumberToObject(outbox, "outbox_max_depth", static_cast<double>(mqtt.outbox_max_depth));
    cJSON_AddNumberToObject(outbox, "outbox_dropped_disconnected", static_cast<double>(mqtt.outbox_dropped_disconnected));
    cJSON_AddNumberToObject(outbox, "outbox_carry_pending", static_cast<double>(mqtt.outbox_carry_pending));
    cJSON_AddNumberToObject(outbox, "outbox_carry_retry_attempts", static_cast<double>(mqtt.outbox_carry_retry_attempts));
    cJSON_AddNumberToObject(outbox, "outbox_carry_drops", static_cast<double>(mqtt.outbox_carry_drops));
}

void add_runtime_links_summary_json(cJSON* root, const metrics_service::RuntimeMetrics& metrics,
                                    const config_store::AppConfig& cfg,
                                    const wifi_manager::WifiStatus& wifi,
                                    const mqtt_service::MqttStatus& mqtt,
                                    const radio_cc1101::RadioCc1101& radio,
                                    const ota_manager::OtaStatus& ota) {
    cJSON_AddStringToObject(root, "mode", cfg.wifi.is_configured() ? "normal" : "provisioning");
    cJSON_AddStringToObject(root, "firmware_version", ota.current_version);
    cJSON* wifi_obj = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi_obj, "state", wifi_state_name(wifi.state));
    cJSON_AddStringToObject(wifi_obj, "ip_address", wifi.ip_address);
    cJSON_AddNumberToObject(wifi_obj, "rssi_dbm", static_cast<double>(wifi.rssi_dbm));
    cJSON_AddStringToObject(wifi_obj, "ssid", wifi.ssid);
    cJSON_AddNumberToObject(wifi_obj, "reconnect_count", static_cast<double>(wifi.reconnect_count));
    cJSON* mqtt_obj = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddStringToObject(mqtt_obj, "state", mqtt_state_name(mqtt.state));
    cJSON_AddNumberToObject(mqtt_obj, "publish_count", static_cast<double>(mqtt.publish_count));
    cJSON_AddNumberToObject(mqtt_obj, "publish_failures", static_cast<double>(mqtt.publish_failures));
    cJSON_AddNumberToObject(mqtt_obj, "reconnect_count", static_cast<double>(mqtt.reconnect_count));
    cJSON* radio_obj = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddStringToObject(radio_obj, "state", radio_state_name(radio.state()));
    cJSON_AddNumberToObject(radio_obj, "frames_received", static_cast<double>(metrics.sessions.completed));
    cJSON_AddNumberToObject(radio_obj, "frames_crc_ok", static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(radio_obj, "frames_crc_fail", static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(radio_obj, "telegrams_validated",
                            static_cast<double>(metrics.sessions.link_validated));
    cJSON_AddNumberToObject(radio_obj, "telegrams_rejected",
                            static_cast<double>(metrics.sessions.link_rejected));
    cJSON_AddNumberToObject(radio_obj, "sessions_aborted",
                            static_cast<double>(metrics.sessions.incomplete));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_available_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_available));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_unavailable_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_unavailable));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_ok_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_ok));
    cJSON_AddNumberToObject(radio_obj, "radio_crc_fail_sessions",
                            static_cast<double>(metrics.sessions.radio_crc_fail));
    cJSON_AddNumberToObject(radio_obj, "frames_incomplete", static_cast<double>(metrics.sessions.incomplete));
    cJSON_AddNumberToObject(radio_obj, "frames_dropped_too_long",
                            static_cast<double>(metrics.sessions.dropped_too_long));
}

esp_err_t send_status_full_chunked(httpd_req_t* req, const health_monitor::HealthSnapshot& health,
                                   const metrics_service::RuntimeMetrics& metrics,
                                   const config_store::AppConfig& cfg,
                                   const config_store::ConfigRuntimeStatus& cfg_runtime,
                                   const wifi_manager::WifiStatus& wifi,
                                   const mqtt_service::MqttStatus& mqtt,
                                   const radio_cc1101::RadioCc1101& radio,
                                   const ota_manager::OtaStatus& ota) {
    httpd_resp_set_type(req, "application/json");
    apply_json_security_headers(req);
    httpd_resp_set_status(req, "200 OK");

    esp_err_t err = send_chunk(req, "{");
    if (err != ESP_OK) {
        return err;
    }

    bool first_field = true;
    err = send_json_object_section(
        req, first_field,
        [&](cJSON* root) { add_runtime_links_json(root, metrics, cfg, wifi, mqtt, radio, ota); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field,
                                   [&](cJSON* root) { add_health_json(root, health); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field,
                                   [&](cJSON* root) { add_metrics_json(root, metrics); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field, [&](cJSON* root) { add_time_json(root); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field,
                                   [&](cJSON* root) { add_protocol_runtime_json(root); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field,
                                   [&](cJSON* root) { add_security_json(root, cfg); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_object_section(req, first_field,
                                   [&](cJSON* root) { add_config_runtime_json(root, cfg_runtime); });
    if (err != ESP_OK) {
        return err;
    }
    err = send_chunk(req, "}");
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}
} // namespace

esp_err_t handle_status(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) return send_json(req, 500, "{\"error\":\"health_snapshot_failed\"}");
    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) return send_json(req, 500, "{\"error\":\"metrics_snapshot_failed\"}");

    auto health = std::make_unique<health_monitor::HealthSnapshot>(health_res.value());
    auto metrics = std::make_unique<metrics_service::RuntimeMetrics>(metrics_res.value());
    auto cfg = std::make_unique<config_store::AppConfig>(config_store::ConfigStore::instance().config());
    auto wifi = std::make_unique<wifi_manager::WifiStatus>(wifi_manager::WifiManager::instance().status());
    auto mqtt = std::make_unique<mqtt_service::MqttStatus>(mqtt_service::MqttService::instance().status());
    auto ota = std::make_unique<ota_manager::OtaStatus>(ota_manager::OtaManager::instance().status());
    const auto& radio = radio_cc1101::RadioCc1101::instance();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    add_runtime_links_summary_json(root.get(), *metrics, *cfg, *wifi, *mqtt, radio, *ota);
    add_health_summary_json(root.get(), *health);
    add_metrics_summary_json(root.get(), *metrics);
    add_time_summary_json(root.get());
    add_protocol_runtime_json(root.get());
    return send_json_root(req, 200, root);
}

esp_err_t handle_status_full(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) return send_json(req, 500, "{\"error\":\"health_snapshot_failed\"}");
    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) return send_json(req, 500, "{\"error\":\"metrics_snapshot_failed\"}");

    auto health = std::make_unique<health_monitor::HealthSnapshot>(health_res.value());
    auto metrics = std::make_unique<metrics_service::RuntimeMetrics>(metrics_res.value());
    auto cfg = std::make_unique<config_store::AppConfig>(config_store::ConfigStore::instance().config());
    auto cfg_runtime =
        std::make_unique<config_store::ConfigRuntimeStatus>(config_store::ConfigStore::instance().runtime_status());
    auto wifi = std::make_unique<wifi_manager::WifiStatus>(wifi_manager::WifiManager::instance().status());
    auto mqtt = std::make_unique<mqtt_service::MqttStatus>(mqtt_service::MqttService::instance().status());
    auto ota = std::make_unique<ota_manager::OtaStatus>(ota_manager::OtaManager::instance().status());
    const auto& radio = radio_cc1101::RadioCc1101::instance();

    return send_status_full_chunked(req, *health, *metrics, *cfg, *cfg_runtime, *wifi, *mqtt, radio,
                                    *ota);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
