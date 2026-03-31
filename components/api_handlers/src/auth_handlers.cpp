#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include "provisioning_manager/provisioning_manager.hpp"

#include <cstdio>
#include <cstring>

namespace api_handlers::detail {

esp_err_t handle_bootstrap(httpd_req_t* req) {
    const auto cfg = config_store::ConfigStore::instance().config();
    const bool provisioning = !cfg.wifi.is_configured();
    const bool password_set = cfg.auth.has_password();
    const bool bootstrap_login_open = provisioning && !password_set;
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddStringToObject(root.get(), "mode", provisioning ? "provisioning" : "normal");
    cJSON_AddBoolToObject(root.get(), "provisioning", provisioning);
    cJSON_AddBoolToObject(root.get(), "password_set", password_set);
    cJSON_AddBoolToObject(root.get(), "provisioning_ap_open", provisioning);
    cJSON_AddBoolToObject(root.get(), "bootstrap_login_open", bootstrap_login_open);
    cJSON_AddBoolToObject(root.get(), "provisioning_insecure_window", bootstrap_login_open);
    return send_json_root(req, 200, root);
}

esp_err_t handle_bootstrap_setup(httpd_req_t* req) {
    const auto current_cfg = config_store::ConfigStore::instance().config();
    if (current_cfg.wifi.is_configured() || current_cfg.auth.has_password()) {
        return send_json(req, 409,
                         "{\"error\":\"bootstrap_setup_not_allowed\","
                         "\"detail\":\"already_configured\"}");
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/json\"}");
    }

    std::string body;
    if (!read_request_body(req, body, 16384)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    config_store::AppConfig cfg = current_cfg;
    apply_config_json(root.get(), cfg);
    if (!cfg.wifi.is_configured()) {
        return send_json(req, 400, "{\"error\":\"wifi_not_configured\"}");
    }
    if (!cfg.auth.has_password()) {
        return send_json(req, 400, "{\"error\":\"admin_password_required\"}");
    }

    auto save_result = config_store::ConfigStore::instance().save(cfg);
    if (save_result.is_error()) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    if (!save_result.value().valid) {
        return send_validation_issues(req, save_result.value());
    }

    auto& prov = provisioning_manager::ProvisioningManager::instance();
    const bool provisioning_completed = prov.is_active() && prov.complete().is_ok();
    JsonPtr ok = make_json_object();
    if (!ok) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddBoolToObject(ok.get(), "ok", true);
    cJSON_AddBoolToObject(ok.get(), "reboot_required", true);
    cJSON_AddBoolToObject(ok.get(), "provisioning_completed", provisioning_completed);
    return send_json_root(req, 200, ok);
}

esp_err_t handle_auth_login(httpd_req_t* req) {
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    const cJSON* pw = cJSON_GetObjectItemCaseSensitive(root.get(), "password");
    const char* password = (pw && cJSON_IsString(pw)) ? pw->valuestring : nullptr;
    const uint32_t client_id = client_id_from_request(req);
    auto result = auth_service::AuthService::instance().login(password, client_id);
    if (result.is_error()) {
        if (result.error() == common::ErrorCode::AuthRateLimited) {
            const int32_t retry_after =
                auth_service::AuthService::instance().retry_after_seconds(client_id);
            JsonPtr rate = make_json_object();
            if (!rate) {
                return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
            }
            cJSON_AddStringToObject(rate.get(), "error", "rate_limited");
            cJSON_AddNumberToObject(rate.get(), "retry_after_s", static_cast<double>(retry_after));
            char retry_after_hdr[16];
            std::snprintf(retry_after_hdr, sizeof(retry_after_hdr), "%ld",
                          static_cast<long>(retry_after));
            httpd_resp_set_hdr(req, "Retry-After", retry_after_hdr);
            return send_json_root(req, 429, rate);
        }
        return send_json(req, 401, "{\"error\":\"login_failed\"}");
    }

    const auth_service::SessionInfo& s = result.value();
    JsonPtr ok = make_json_object();
    if (!ok) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddStringToObject(ok.get(), "token", s.token);
    cJSON_AddNumberToObject(ok.get(), "created_epoch_s", static_cast<double>(s.created_epoch_s));
    cJSON_AddNumberToObject(ok.get(), "expires_epoch_s", static_cast<double>(s.expires_epoch_s));
    cJSON_AddBoolToObject(ok.get(), "valid", true);
    return send_json_root(req, 200, ok);
}

esp_err_t handle_auth_logout(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    return auth_service::AuthService::instance().logout().is_ok()
               ? send_json(req, 200, "{\"ok\":true}")
               : send_json(req, 500, "{\"error\":\"logout_failed\"}");
}

esp_err_t handle_auth_password(httpd_req_t* req) {
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
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    const cJSON* current = cJSON_GetObjectItemCaseSensitive(root.get(), "current_password");
    const cJSON* next = cJSON_GetObjectItemCaseSensitive(root.get(), "new_password");
    const char* current_password =
        (current && cJSON_IsString(current)) ? current->valuestring : nullptr;
    const char* new_password = (next && cJSON_IsString(next)) ? next->valuestring : nullptr;
    if (!new_password || new_password[0] == '\0' ||
        std::strlen(new_password) > auth_service::kMaxPasswordLength) {
        return send_json(req, 400, "{\"error\":\"invalid_new_password\"}");
    }

    const auto cfg = config_store::ConfigStore::instance().config();
    if (cfg.auth.has_password() && !auth_service::AuthService::verify_password(
                                       current_password, cfg.auth.admin_password_hash)) {
        return send_json(req, 401, "{\"error\":\"current_password_invalid\"}");
    }

    auto hash_res = auth_service::AuthService::hash_password(new_password);
    if (hash_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"password_hash_failed\"}");
    }

    config_store::AppConfig updated = cfg;
    assign_admin_password_hash(updated.auth, hash_res.value().c_str());
    auto save = config_store::ConfigStore::instance().save(updated);
    if (save.is_error() || !save.value().valid) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    (void)auth_service::AuthService::instance().logout();
    return send_json(req, 200, "{\"ok\":true,\"relogin_required\":true}");
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
