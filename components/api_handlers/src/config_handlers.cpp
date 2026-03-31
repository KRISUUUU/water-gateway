#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include "provisioning_manager/provisioning_manager.hpp"

#include <cstring>

namespace api_handlers::detail {

esp_err_t handle_config_get(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto cfg = config_store::ConfigStore::instance().config();
    const std::string json = config_to_json_redacted(cfg);
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_config_post(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/json\"}");
    }

    std::string body;
    if (!read_request_body(req, body, 32768)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    const auto previous_cfg = config_store::ConfigStore::instance().config();
    auto cfg = previous_cfg;
    apply_config_json(root.get(), cfg);

    auto save_result = config_store::ConfigStore::instance().save(cfg);
    if (save_result.is_error()) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    if (!save_result.value().valid) {
        return send_validation_issues(req, save_result.value());
    }

    auto& prov = provisioning_manager::ProvisioningManager::instance();
    const bool provisioning_completed = prov.is_active() && cfg.wifi.is_configured() &&
                                        prov.complete().is_ok();
    const bool auth_changed =
        std::strcmp(previous_cfg.auth.admin_password_hash, cfg.auth.admin_password_hash) != 0 ||
        previous_cfg.auth.session_timeout_s != cfg.auth.session_timeout_s;
    if (auth_changed) {
        (void)auth_service::AuthService::instance().logout();
    }

    JsonPtr ok = make_json_object();
    if (!ok) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddBoolToObject(ok.get(), "ok", true);
    cJSON_AddBoolToObject(ok.get(), "reboot_required", true);
    cJSON_AddBoolToObject(ok.get(), "relogin_required", auth_changed);
    cJSON_AddBoolToObject(ok.get(), "provisioning_completed", provisioning_completed);
    return send_json_root(req, 200, ok);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
