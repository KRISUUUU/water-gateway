#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "ota_manager/ota_manager.hpp"

namespace api_handlers::detail {

esp_err_t handle_ota_status(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto st = ota_manager::OtaManager::instance().status();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddStringToObject(root.get(), "state", ota_state_name(st.state));
    cJSON_AddStringToObject(root.get(), "message", st.message);
    cJSON_AddNumberToObject(root.get(), "progress_pct", static_cast<double>(st.progress_pct));
    cJSON_AddStringToObject(root.get(), "current_version", st.current_version);
    cJSON_AddBoolToObject(root.get(), "boot_pending_verify", st.boot_pending_verify);
    cJSON_AddBoolToObject(root.get(), "boot_marked_valid", st.boot_marked_valid);
    cJSON_AddNumberToObject(root.get(), "boot_mark_attempts",
                            static_cast<double>(st.boot_mark_attempts));
    cJSON_AddNumberToObject(root.get(), "boot_mark_failures",
                            static_cast<double>(st.boot_mark_failures));
    cJSON_AddNumberToObject(root.get(), "last_boot_mark_error",
                            static_cast<double>(st.last_boot_mark_error));
    cJSON_AddStringToObject(root.get(), "boot_validation_note", st.boot_validation_note);
    return send_json_root(req, 200, root);
}

esp_err_t handle_ota_upload(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    if (!request_content_type_is_binary(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/octet-stream\"}");
    }
    if (req->content_len <= 0) {
        return send_json(req, 400, "{\"error\":\"empty_body\"}");
    }

    auto begin =
        ota_manager::OtaManager::instance().begin_upload(static_cast<size_t>(req->content_len));
    if (begin.is_error()) {
        if (begin.error() == common::ErrorCode::OtaAlreadyInProgress) {
            return send_json(req, 409, "{\"error\":\"ota_in_progress\"}");
        }
        if (begin.error() == common::ErrorCode::OtaImageTooLarge) {
            return send_json(req, 413, "{\"error\":\"image_too_large\"}");
        }
        return send_json(req, 500, "{\"error\":\"ota_begin_failed\"}");
    }

    constexpr size_t kChunkSize = 2048;
    char chunk[kChunkSize];
    int remaining = req->content_len;
    while (remaining > 0) {
        const int to_read =
            remaining > static_cast<int>(kChunkSize) ? static_cast<int>(kChunkSize) : remaining;
        const int received = httpd_req_recv(req, chunk, to_read);
        if (received <= 0) {
            ota_manager::OtaManager::instance().abort_upload();
            return send_json(req, 500, "{\"error\":\"upload_read_failed\"}");
        }
        auto wr = ota_manager::OtaManager::instance().write_chunk(
            reinterpret_cast<const uint8_t*>(chunk), static_cast<size_t>(received));
        if (wr.is_error()) {
            return send_json(req, 500, "{\"error\":\"ota_write_failed\"}");
        }
        remaining -= received;
    }

    return ota_manager::OtaManager::instance().finalize_upload().is_ok()
               ? send_json(req, 200,
                           "{\"ok\":true,\"reboot_required\":true,"
                           "\"detail\":\"ota_upload_complete_reboot_to_activate\"}")
               : send_json(req, 500, "{\"error\":\"ota_finalize_failed\"}");
}

esp_err_t handle_ota_url(httpd_req_t* req) {
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

    const cJSON* url = cJSON_GetObjectItemCaseSensitive(root.get(), "url");
    const std::string url_copy = (url && cJSON_IsString(url) && url->valuestring) ? url->valuestring : "";
    if (url_copy.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_url\"}");
    }
    if (!has_https_scheme(url_copy)) {
        return send_json(req, 400,
                         "{\"error\":\"invalid_url_scheme\",\"detail\":\"https_required\"}");
    }
    auto begin = ota_manager::OtaManager::instance().begin_url_ota_async(url_copy.c_str());
    if (begin.is_ok()) {
        return send_json(req, 202, "{\"ok\":true,\"started\":true}");
    }
    if (begin.error() == common::ErrorCode::OtaAlreadyInProgress) {
        return send_json(req, 409, "{\"error\":\"ota_in_progress\"}");
    }
    return send_json(req, 400, "{\"error\":\"ota_begin_failed\"}");
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
