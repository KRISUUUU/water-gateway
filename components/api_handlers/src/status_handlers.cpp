#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "common/error.hpp"
#include "common/security_posture.hpp"
#include "config_store/config_store.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "ntp_service/ntp_service.hpp"

namespace api_handlers::detail {

namespace {
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
    cJSON_AddNumberToObject(tasks, "radio_read_success_count", static_cast<double>(metrics.tasks.radio_read_success_count));
    cJSON_AddNumberToObject(tasks, "radio_read_not_found_count", static_cast<double>(metrics.tasks.radio_read_not_found_count));
    cJSON_AddNumberToObject(tasks, "radio_read_timeout_count", static_cast<double>(metrics.tasks.radio_read_timeout_count));
    cJSON_AddNumberToObject(tasks, "radio_read_error_count", static_cast<double>(metrics.tasks.radio_read_error_count));
    cJSON_AddNumberToObject(tasks, "radio_not_found_streak", static_cast<double>(metrics.tasks.radio_not_found_streak));
    cJSON_AddNumberToObject(tasks, "radio_not_found_streak_peak", static_cast<double>(metrics.tasks.radio_not_found_streak_peak));
    cJSON_AddNumberToObject(tasks, "radio_poll_iterations", static_cast<double>(metrics.tasks.radio_poll_iterations));
    cJSON_AddNumberToObject(tasks, "radio_timeout_streak", static_cast<double>(metrics.tasks.radio_timeout_streak));
    cJSON_AddNumberToObject(tasks, "radio_timeout_streak_peak", static_cast<double>(metrics.tasks.radio_timeout_streak_peak));
    cJSON_AddNumberToObject(tasks, "radio_stall_count", static_cast<double>(metrics.tasks.radio_stall_count));
    cJSON_AddNumberToObject(tasks, "pipeline_stall_count", static_cast<double>(metrics.tasks.pipeline_stall_count));
    cJSON_AddNumberToObject(tasks, "mqtt_stall_count", static_cast<double>(metrics.tasks.mqtt_stall_count));
    cJSON_AddNumberToObject(tasks, "watchdog_register_errors", static_cast<double>(metrics.tasks.watchdog_register_errors));
    cJSON_AddNumberToObject(tasks, "watchdog_feed_errors", static_cast<double>(metrics.tasks.watchdog_feed_errors));
    cJSON_AddNumberToObject(tasks, "radio_stack_hwm_words", static_cast<double>(metrics.tasks.radio_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "pipeline_stack_hwm_words", static_cast<double>(metrics.tasks.pipeline_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "mqtt_stack_hwm_words", static_cast<double>(metrics.tasks.mqtt_stack_hwm_words));
    cJSON_AddNumberToObject(tasks, "health_stack_hwm_words", static_cast<double>(metrics.tasks.health_stack_hwm_words));
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
    cJSON_AddNumberToObject(radio_obj, "rx_read_calls", static_cast<double>(counters.rx_read_calls));
    cJSON_AddNumberToObject(radio_obj, "rx_not_found", static_cast<double>(counters.rx_not_found));
    cJSON_AddNumberToObject(radio_obj, "rx_timeouts", static_cast<double>(counters.rx_timeouts));
    cJSON_AddNumberToObject(radio_obj, "frames_received", static_cast<double>(counters.frames_received));
    cJSON_AddNumberToObject(radio_obj, "frames_crc_ok", static_cast<double>(counters.frames_crc_ok));
    cJSON_AddNumberToObject(radio_obj, "frames_crc_fail", static_cast<double>(counters.frames_crc_fail));
    cJSON_AddNumberToObject(radio_obj, "frames_incomplete", static_cast<double>(counters.frames_incomplete));
    cJSON_AddNumberToObject(radio_obj, "frames_dropped_too_long", static_cast<double>(counters.frames_dropped_too_long));
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

    const auto cfg = config_store::ConfigStore::instance().config();
    const auto cfg_runtime = config_store::ConfigStore::instance().runtime_status();
    const auto wifi = wifi_manager::WifiManager::instance().status();
    const auto mqtt = mqtt_service::MqttService::instance().status();
    const auto ota = ota_manager::OtaManager::instance().status();
    const auto& radio = radio_cc1101::RadioCc1101::instance();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    add_health_json(root.get(), health_res.value());
    add_metrics_json(root.get(), metrics_res.value());
    add_time_json(root.get());
    add_security_json(root.get(), cfg);
    add_config_runtime_json(root.get(), cfg_runtime);
    add_runtime_links_json(root.get(), metrics_res.value(), cfg, wifi, mqtt, radio, ota);
    return send_json_root(req, 200, root);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
