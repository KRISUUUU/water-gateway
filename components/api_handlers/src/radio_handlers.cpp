#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "common/error.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "radio_state_machine/radio_state_machine.hpp"

namespace api_handlers::detail {

esp_err_t handle_diagnostics_radio(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto& rsm = radio_state_machine::RadioStateMachine::instance();
    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    if (snap_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"diagnostics_snapshot_failed\"}");
    }

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
    JsonPtr diag(cJSON_Parse(diagnostics_service::DiagnosticsService::to_json(snap_res.value()).c_str()),
                 cJSON_Delete);
    if (!diag) {
        return send_json(req, 500, "{\"error\":\"diagnostics_json_invalid\"}");
    }
    cJSON_AddItemToObject(root.get(), "diagnostics", diag.release());
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
