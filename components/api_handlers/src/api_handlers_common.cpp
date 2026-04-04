#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "http_server/http_server.hpp"

#include "common/security_posture.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_system.h"

namespace api_handlers::detail {

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

void assign_admin_password_hash(config_store::AuthConfig& auth, const char* hash_cstr) {
    if (!hash_cstr) {
        auth.admin_password_hash[0] = '\0';
        return;
    }
    std::strncpy(auth.admin_password_hash, hash_cstr, sizeof(auth.admin_password_hash) - 1);
    auth.admin_password_hash[sizeof(auth.admin_password_hash) - 1] = '\0';
}

bool is_hex_string(const std::string& s) {
    for (unsigned char c : s) {
        if (std::isxdigit(c) == 0) {
            return false;
        }
    }
    return true;
}

void json_escape_append(std::string& out, const std::string& s) {
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
            out += buf;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
}

esp_err_t send_json_chunk_row(httpd_req_t* req, std::string& row) {
    if (row.empty()) {
        return ESP_OK;
    }
    const esp_err_t e =
        httpd_resp_send_chunk(req, row.data(), static_cast<ssize_t>(row.size()));
    row.clear();
    return e;
}

JsonPtr make_json_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
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

esp_err_t send_json_root(httpd_req_t* req, int status_code, cJSON* root) {
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    return send_json(req, status_code, printed.get());
}

esp_err_t send_json_root(httpd_req_t* req, int status_code, const JsonPtr& root) {
    return send_json_root(req, status_code, root.get());
}

std::string query_param(const char* uri, const char* key) {
    if (!uri || !key || key[0] == '\0') {
        return "";
    }
    std::string s(uri);
    const size_t q = s.find('?');
    if (q == std::string::npos || q + 1 >= s.size()) {
        return "";
    }
    const std::string query = s.substr(q + 1);
    const std::string needle = std::string(key) + "=";
    size_t pos = query.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos += needle.size();
    size_t end = query.find('&', pos);
    if (end == std::string::npos) {
        end = query.size();
    }
    return query.substr(pos, end - pos);
}

void apply_json_security_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
}

esp_err_t send_json(httpd_req_t* req, int status_code, const char* body) {
    httpd_resp_set_type(req, "application/json");
    apply_json_security_headers(req);
    const char* status = "200 OK";
    switch (status_code) {
    case 200:
        status = "200 OK";
        break;
    case 201:
        status = "201 Created";
        break;
    case 400:
        status = "400 Bad Request";
        break;
    case 401:
        status = "401 Unauthorized";
        break;
    case 404:
        status = "404 Not Found";
        break;
    case 409:
        status = "409 Conflict";
        break;
    case 413:
        status = "413 Payload Too Large";
        break;
    case 415:
        status = "415 Unsupported Media Type";
        break;
    case 429:
        status = "429 Too Many Requests";
        break;
    case 500:
        status = "500 Internal Server Error";
        break;
    case 501:
        status = "501 Not Implemented";
        break;
    default:
        status = "500 Internal Server Error";
        break;
    }
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

bool read_request_body(httpd_req_t* req, std::string& out, size_t max_len) {
    const size_t len = req->content_len;
    if (len > max_len) {
        return false;
    }
    out.resize(len);
    size_t read = 0;
    while (read < len) {
        const int r = httpd_req_recv(req, &out[read], len - read);
        if (r <= 0) {
            return false;
        }
        read += static_cast<size_t>(r);
    }
    return true;
}

bool request_content_type_is_json(httpd_req_t* req) {
    const size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        return true;
    }
    std::string value;
    value.resize(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Content-Type", value.data(), value.size()) != ESP_OK) {
        return false;
    }
    std::string lower = value.c_str();
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("application/json") != std::string::npos ||
           lower.find("text/json") != std::string::npos;
}

bool request_content_type_is_binary(httpd_req_t* req) {
    const size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        return true;
    }
    std::string value;
    value.resize(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Content-Type", value.data(), value.size()) != ESP_OK) {
        return false;
    }
    std::string lower = value.c_str();
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("application/octet-stream") != std::string::npos ||
           lower.find("application/x-binary") != std::string::npos;
}

esp_err_t require_auth(httpd_req_t* req) {
    if (!http_server::HttpServer::instance().authorize_request(req)) {
        return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    }
    return ESP_OK;
}

uint32_t client_id_from_request(httpd_req_t* req) {
    if (!req) {
        return 0;
    }
    // ESP-IDF exposes only httpd_req_to_sockfd() here, so use the active session
    // socket fd as a per-client proxy. +1 preserves client_id=0 as global fallback.
    const int sockfd = httpd_req_to_sockfd(req);
    return sockfd >= 0 ? static_cast<uint32_t>(sockfd) + 1U : 0U;
}

const char* log_severity_name(persistent_log_buffer::LogSeverity s) {
    using persistent_log_buffer::LogSeverity;
    switch (s) {
    case LogSeverity::Debug:
        return "debug";
    case LogSeverity::Info:
        return "info";
    case LogSeverity::Warning:
        return "warning";
    case LogSeverity::Error:
        return "error";
    default:
        return "unknown";
    }
}

const char* mqtt_state_name(mqtt_service::MqttState s) {
    using mqtt_service::MqttState;
    switch (s) {
    case MqttState::Uninitialized:
        return "uninitialized";
    case MqttState::Disconnected:
        return "disconnected";
    case MqttState::Connecting:
        return "connecting";
    case MqttState::Connected:
        return "connected";
    case MqttState::Error:
        return "error";
    default:
        return "unknown";
    }
}

const char* rsm_state_name(radio_state_machine::RsmState s) {
    using radio_state_machine::RsmState;
    switch (s) {
    case RsmState::Uninitialized:
        return "uninitialized";
    case RsmState::Initializing:
        return "initializing";
    case RsmState::Receiving:
        return "receiving";
    case RsmState::Error:
        return "error";
    case RsmState::Recovering:
        return "recovering";
    default:
        return "unknown";
    }
}

const char* wifi_state_name(wifi_manager::WifiState s) {
    using wifi_manager::WifiState;
    switch (s) {
    case WifiState::Uninitialized:
        return "uninitialized";
    case WifiState::Disconnected:
        return "disconnected";
    case WifiState::Connecting:
        return "connecting";
    case WifiState::Connected:
        return "connected";
    case WifiState::ApMode:
        return "ap_mode";
    default:
        return "unknown";
    }
}

const char* radio_state_name(radio_cc1101::RadioState s) {
    using radio_cc1101::RadioState;
    switch (s) {
    case RadioState::Uninitialized:
        return "uninitialized";
    case RadioState::Idle:
        return "idle";
    case RadioState::Receiving:
        return "receiving";
    case RadioState::Error:
        return "error";
    default:
        return "unknown";
    }
}

const char* ota_state_name(ota_manager::OtaState s) {
    using ota_manager::OtaState;
    switch (s) {
    case OtaState::Idle:
        return "idle";
    case OtaState::InProgress:
        return "in_progress";
    case OtaState::Validating:
        return "validating";
    case OtaState::Rebooting:
        return "rebooting";
    case OtaState::Failed:
        return "failed";
    default:
        return "unknown";
    }
}

const char* config_load_source_name(config_store::ConfigLoadSource s) {
    using config_store::ConfigLoadSource;
    switch (s) {
    case ConfigLoadSource::None:
        return "none";
    case ConfigLoadSource::PrimaryNvs:
        return "primary_nvs";
    case ConfigLoadSource::BackupNvs:
        return "backup_nvs";
    case ConfigLoadSource::Defaults:
        return "defaults";
    default:
        return "unknown";
    }
}

bool has_https_scheme(const std::string& url) {
    static constexpr const char* kHttps = "https://";
    return url.size() >= 8 && url.compare(0, 8, kHttps) == 0;
}

esp_err_t send_validation_issues(httpd_req_t* req, const config_store::ValidationResult& vr) {
    JsonPtr err_root = make_json_object();
    if (!err_root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddBoolToObject(err_root.get(), "ok", false);
    cJSON* issues = cJSON_AddArrayToObject(err_root.get(), "issues");
    if (!issues) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    for (const auto& issue : vr.issues) {
        cJSON* it = cJSON_CreateObject();
        if (!it) {
            return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
        }
        cJSON_AddStringToObject(it, "field", issue.field.c_str());
        cJSON_AddStringToObject(it, "message", issue.message.c_str());
        cJSON_AddStringToObject(
            it, "severity",
            issue.severity == config_store::ValidationSeverity::Error ? "error" : "warning");
        cJSON_AddItemToArray(issues, it);
    }
    return send_json_root(req, 400, err_root);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
