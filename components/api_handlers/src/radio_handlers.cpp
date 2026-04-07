#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "common/error.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "meter_registry/meter_registry.hpp"
#include "protocol_driver/radio_profile_manager.hpp"
#include "protocol_driver/radio_runtime_plan.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

namespace api_handlers::detail {

namespace {

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

const rf_diagnostics::RfDiagnosticRecord* newest_rf_reject(
    const rf_diagnostics::RfDiagnosticsSnapshot* snapshot) {
    if (!snapshot || snapshot->count == 0) {
        return nullptr;
    }
    return &snapshot->records[snapshot->count - 1];
}

} // namespace

esp_err_t handle_diagnostics_radio(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto& rsm = radio_state_machine::RadioStateMachine::instance();
    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot_allocated();
    if (snap_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"diagnostics_snapshot_failed\"}");
    }
    const auto& diag_snapshot = snap_res.value();

    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON* rsm_obj = cJSON_AddObjectToObject(root.get(), "rsm");
    cJSON_AddStringToObject(rsm_obj, "state", rsm_state_name(rsm.state()));
    cJSON_AddNumberToObject(rsm_obj, "consecutive_errors",
                            static_cast<double>(rsm.consecutive_errors()));
    cJSON_AddNumberToObject(rsm_obj, "soft_failure_streak",
                            static_cast<double>(rsm.soft_failure_streak()));
    cJSON_AddNumberToObject(rsm_obj, "recovery_attempts",
                            static_cast<double>(rsm.recovery_attempts()));
    cJSON_AddNumberToObject(rsm_obj, "recovery_failures",
                            static_cast<double>(rsm.recovery_failures()));
    cJSON_AddStringToObject(rsm_obj, "last_recovery_reason",
                            common::error_code_to_string(rsm.last_recovery_reason()));
    cJSON_AddNumberToObject(rsm_obj, "last_recovery_reason_code",
                            static_cast<double>(static_cast<int>(rsm.last_recovery_reason())));
    JsonPtr diag(cJSON_Parse(diagnostics_service::DiagnosticsService::to_json(*diag_snapshot).c_str()),
                 cJSON_Delete);
    if (!diag) {
        return send_json(req, 500, "{\"error\":\"diagnostics_json_invalid\"}");
    }
    cJSON_AddItemToObject(root.get(), "diagnostics", diag.release());

    // Scheduler / profile status (primary radio active; secondary reserved)
    const auto sched = protocol_driver::RadioProfileManager::instance().status();
    const auto runtime_plan = protocol_driver::RadioRuntimePlan::single_radio(
        sched.scheduler_mode, sched.enabled_profiles);
    const auto recent_telegrams =
        meter_registry::MeterRegistry::instance().recent_telegrams(
            meter_registry::TelegramFilter::All);
    const auto tmode_recent = summarize_recent_protocol(recent_telegrams, "WMBUS_T");
    const auto prios_recent = summarize_recent_prios(recent_telegrams);
    auto rf_snapshot = rf_diagnostics::RfDiagnosticsService::instance().snapshot_allocated();
    const auto* last_tmode_reject = newest_rf_reject(rf_snapshot.get());
    const auto prios_stats = wmbus_prios_rx::PriosCaptureService::instance().stats();
    const std::string last_tmode_reject_reason =
        last_tmode_reject
            ? rf_diagnostics::RfDiagnosticsService::reject_reason_to_string(
                  last_tmode_reject->reject_reason)
            : "none";
    cJSON* sched_obj = cJSON_AddObjectToObject(root.get(), "scheduler");
    cJSON_AddNumberToObject(sched_obj, "radio_instance",
                            static_cast<double>(sched.radio_instance));
    cJSON_AddBoolToObject(sched_obj, "single_radio_mode", runtime_plan.single_radio_mode);
    cJSON_AddStringToObject(sched_obj, "mode",
                            protocol_driver::radio_scheduler_mode_to_string(sched.scheduler_mode));
    cJSON_AddStringToObject(sched_obj, "preferred_profile",
                            protocol_driver::radio_profile_id_to_string(
                                sched.preferred_profile_id));
    cJSON_AddStringToObject(sched_obj, "selected_profile",
                            protocol_driver::radio_profile_id_to_string(
                                sched.selected_profile_id));
    cJSON_AddStringToObject(sched_obj, "active_profile",
                            protocol_driver::radio_profile_id_to_string(sched.active_profile_id));
    cJSON_AddStringToObject(sched_obj, "active_protocol",
                            protocol_name_for_profile(sched.active_profile_id));
    cJSON_AddStringToObject(sched_obj, "last_switch_reason",
                            protocol_driver::scheduler_switch_reason_to_string(
                                sched.last_switch_reason));
    cJSON_AddStringToObject(sched_obj, "last_apply_status",
                            protocol_driver::profile_apply_status_to_string(
                                sched.last_apply_status));
    cJSON_AddNumberToObject(sched_obj, "profile_switch_count",
                            static_cast<double>(sched.profile_switch_count));
    cJSON_AddNumberToObject(sched_obj, "profile_apply_count",
                            static_cast<double>(sched.profile_apply_count));
    cJSON_AddNumberToObject(sched_obj, "profile_apply_failures",
                            static_cast<double>(sched.profile_apply_failures));
    cJSON_AddNumberToObject(sched_obj, "enabled_profiles_mask",
                            static_cast<double>(sched.enabled_profiles));
    cJSON_AddStringToObject(sched_obj, "last_wake_source",
                            protocol_driver::runtime_wake_source_to_string(
                                sched.last_wake_source));
    cJSON_AddNumberToObject(sched_obj, "irq_wake_count",
                            static_cast<double>(sched.irq_wake_count));
    cJSON_AddNumberToObject(sched_obj, "fallback_wake_count",
                            static_cast<double>(sched.fallback_wake_count));

    // Enabled profile list as human-readable strings
    cJSON* profile_arr = cJSON_AddArrayToObject(sched_obj, "enabled_profiles");
    for (uint8_t bit = 1; bit < 8; ++bit) {
        if (sched.enabled_profiles & (1U << bit)) {
            const auto pid = static_cast<protocol_driver::RadioProfileId>(bit);
            cJSON_AddItemToArray(profile_arr,
                                 cJSON_CreateString(
                                     protocol_driver::radio_profile_id_to_string(pid)));
        }
    }

    cJSON* topology_obj = cJSON_AddObjectToObject(root.get(), "topology");
    cJSON_AddBoolToObject(topology_obj, "single_radio_mode", runtime_plan.single_radio_mode);
    cJSON_AddNumberToObject(topology_obj, "active_radio_count",
                            static_cast<double>(runtime_plan.active_radio_count));
    cJSON_AddNumberToObject(topology_obj, "supported_radio_slots",
                            static_cast<double>(protocol_driver::kSupportedRadioInstanceCount));
    cJSON* instances = cJSON_AddArrayToObject(topology_obj, "instances");
    for (const auto& entry : runtime_plan.instances) {
        cJSON* inst = cJSON_CreateObject();
        cJSON_AddNumberToObject(inst, "radio_instance",
                                static_cast<double>(entry.radio_instance));
        cJSON_AddStringToObject(inst, "label",
                                entry.radio_instance == protocol_driver::kRadioInstancePrimary
                                    ? "Primary CC1101"
                                    : "Secondary CC1101");
        cJSON_AddBoolToObject(inst, "present", entry.present);
        cJSON_AddBoolToObject(inst, "enabled", entry.enabled);
        cJSON_AddStringToObject(inst, "scheduler_mode",
                                protocol_driver::radio_scheduler_mode_to_string(
                                    entry.scheduler_mode));
        cJSON_AddNumberToObject(inst, "enabled_profiles_mask",
                                static_cast<double>(entry.enabled_profiles));
        cJSON_AddItemToArray(instances, inst);
    }

    cJSON* operator_obj = cJSON_AddObjectToObject(root.get(), "operator_view");
    cJSON_AddStringToObject(operator_obj, "active_profile",
                            protocol_driver::radio_profile_id_to_string(sched.active_profile_id));
    cJSON_AddStringToObject(operator_obj, "active_protocol",
                            protocol_name_for_profile(sched.active_profile_id));
    cJSON_AddStringToObject(operator_obj, "last_wake_source",
                            protocol_driver::runtime_wake_source_to_string(
                                sched.last_wake_source));

    cJSON* tmode_obj = cJSON_AddObjectToObject(operator_obj, "tmode");
    cJSON_AddNumberToObject(tmode_obj, "recent_accepts",
                            static_cast<double>(tmode_recent.recent_accepts));
    cJSON_AddNumberToObject(tmode_obj, "last_success_timestamp_ms",
                            static_cast<double>(tmode_recent.last_success_timestamp_ms));
    cJSON_AddStringToObject(tmode_obj, "last_success_meter_key", tmode_recent.last_meter_key);
    cJSON_AddNumberToObject(tmode_obj, "last_success_rssi_dbm",
                            static_cast<double>(tmode_recent.last_success_rssi_dbm));
    cJSON_AddNumberToObject(tmode_obj, "last_success_lqi",
                            static_cast<double>(tmode_recent.last_success_lqi));
    cJSON_AddStringToObject(tmode_obj, "last_reject_reason",
                            last_tmode_reject_reason.c_str());
    cJSON_AddNumberToObject(
        tmode_obj, "last_reject_rssi_dbm",
        static_cast<double>(last_tmode_reject ? last_tmode_reject->rssi_dbm : 0));
    cJSON_AddNumberToObject(
        tmode_obj, "last_reject_lqi",
        static_cast<double>(last_tmode_reject ? last_tmode_reject->lqi : 0));

    cJSON* prios_obj = cJSON_AddObjectToObject(operator_obj, "prios");
    cJSON_AddStringToObject(prios_obj, "support_level", "identity_only_capture");
    cJSON_AddBoolToObject(prios_obj, "reading_decode_available", false);
    cJSON_AddNumberToObject(prios_obj, "recent_accepts",
                            static_cast<double>(prios_recent.recent_accepts));
    cJSON_AddNumberToObject(prios_obj, "last_success_timestamp_ms",
                            static_cast<double>(prios_recent.last_success_timestamp_ms));
    cJSON_AddStringToObject(prios_obj, "last_success_meter_key", prios_recent.last_meter_key);
    cJSON_AddNumberToObject(prios_obj, "last_success_rssi_dbm",
                            static_cast<double>(prios_recent.last_success_rssi_dbm));
    cJSON_AddNumberToObject(prios_obj, "last_success_lqi",
                            static_cast<double>(prios_recent.last_success_lqi));
    cJSON_AddStringToObject(prios_obj, "last_reject_reason",
                            prios_stats.last_reject_reason);
    cJSON_AddNumberToObject(prios_obj, "quality_rejections",
                            static_cast<double>(prios_stats.total_quality_rejected));
    cJSON_AddNumberToObject(prios_obj, "noise_rejections",
                            static_cast<double>(prios_stats.total_noise_rejected));
    cJSON_AddNumberToObject(prios_obj, "similarity_rejections",
                            static_cast<double>(prios_stats.total_similarity_rejected));

    return send_json_root(req, 200, root);
}

esp_err_t handle_diagnostics_mqtt(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto st = mqtt_service::MqttService::instance().status();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddStringToObject(root.get(), "state", mqtt_state_name(st.state));
    cJSON_AddNumberToObject(root.get(), "publish_count", static_cast<double>(st.publish_count));
    cJSON_AddNumberToObject(root.get(), "publish_failures",
                            static_cast<double>(st.publish_failures));
    cJSON_AddNumberToObject(root.get(), "reconnect_count", static_cast<double>(st.reconnect_count));
    cJSON_AddNumberToObject(root.get(), "outbox_enqueue_failures",
                            static_cast<double>(st.outbox_enqueue_failures));
    cJSON_AddNumberToObject(root.get(), "outbox_oversize_rejections",
                            static_cast<double>(st.outbox_oversize_rejections));
    cJSON_AddNumberToObject(root.get(), "outbox_max_depth",
                            static_cast<double>(st.outbox_max_depth));
    cJSON_AddNumberToObject(root.get(), "outbox_dropped_disconnected",
                            static_cast<double>(st.outbox_dropped_disconnected));
    cJSON_AddNumberToObject(root.get(), "outbox_carry_pending",
                            static_cast<double>(st.outbox_carry_pending));
    cJSON_AddNumberToObject(root.get(), "outbox_carry_retry_attempts",
                            static_cast<double>(st.outbox_carry_retry_attempts));
    cJSON_AddNumberToObject(root.get(), "outbox_carry_drops",
                            static_cast<double>(st.outbox_carry_drops));
    cJSON_AddNumberToObject(root.get(), "last_publish_epoch_ms",
                            static_cast<double>(st.last_publish_epoch_ms));
    cJSON_AddStringToObject(root.get(), "broker_uri", st.broker_uri);
    return send_json_root(req, 200, root);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
