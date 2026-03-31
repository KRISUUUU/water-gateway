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

namespace {
bool copy_json_string(char* dest, size_t dest_sz, const cJSON* item) {
    if (!dest || dest_sz == 0 || !item || !cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    std::strncpy(dest, item->valuestring, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
    return true;
}
} // namespace

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

std::string config_to_json_redacted(const config_store::AppConfig& c) {
    JsonPtr root = make_json_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddNumberToObject(root.get(), "version", static_cast<double>(c.version));
    cJSON* device = cJSON_AddObjectToObject(root.get(), "device");
    cJSON_AddStringToObject(device, "name", c.device.name);
    cJSON_AddStringToObject(device, "hostname", c.device.hostname);

    cJSON* wifi = cJSON_AddObjectToObject(root.get(), "wifi");
    cJSON_AddStringToObject(wifi, "ssid", c.wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", config_store::kRedactedValue);
    cJSON_AddNumberToObject(wifi, "max_retries", static_cast<double>(c.wifi.max_retries));

    cJSON* mqtt = cJSON_AddObjectToObject(root.get(), "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", c.mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "host", c.mqtt.host);
    cJSON_AddNumberToObject(mqtt, "port", static_cast<double>(c.mqtt.port));
    cJSON_AddStringToObject(mqtt, "username", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "password", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "prefix", c.mqtt.prefix);
    cJSON_AddStringToObject(mqtt, "client_id", c.mqtt.client_id);
    cJSON_AddNumberToObject(mqtt, "qos", static_cast<double>(c.mqtt.qos));
    cJSON_AddBoolToObject(mqtt, "use_tls", c.mqtt.use_tls);

    cJSON* radio = cJSON_AddObjectToObject(root.get(), "radio");
    cJSON_AddNumberToObject(radio, "frequency_khz", static_cast<double>(c.radio.frequency_khz));
    cJSON_AddNumberToObject(radio, "data_rate", static_cast<double>(c.radio.data_rate));
    cJSON_AddBoolToObject(radio, "auto_recovery", c.radio.auto_recovery);

    cJSON* logging = cJSON_AddObjectToObject(root.get(), "logging");
    cJSON_AddNumberToObject(logging, "level", static_cast<double>(c.logging.level));

    cJSON* auth = cJSON_AddObjectToObject(root.get(), "auth");
    cJSON_AddStringToObject(auth, "admin_password", config_store::kRedactedValue);
    cJSON_AddBoolToObject(auth, "password_set", c.auth.has_password());
    cJSON_AddNumberToObject(auth, "session_timeout_s",
                            static_cast<double>(c.auth.session_timeout_s));
    return to_unformatted_json(root.get());
}

void apply_config_json(const cJSON* root, config_store::AppConfig& cfg) {
    const cJSON* device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (device && cJSON_IsObject(device)) {
        copy_json_string(cfg.device.name, sizeof(cfg.device.name),
                         cJSON_GetObjectItemCaseSensitive(device, "name"));
        copy_json_string(cfg.device.hostname, sizeof(cfg.device.hostname),
                         cJSON_GetObjectItemCaseSensitive(device, "hostname"));
    }

    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (wifi && cJSON_IsObject(wifi)) {
        copy_json_string(cfg.wifi.ssid, sizeof(cfg.wifi.ssid),
                         cJSON_GetObjectItemCaseSensitive(wifi, "ssid"));
        const cJSON* pw = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (pw && cJSON_IsString(pw) && pw->valuestring &&
            std::strcmp(pw->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.wifi.password, sizeof(cfg.wifi.password), pw);
        }
        const cJSON* mr = cJSON_GetObjectItemCaseSensitive(wifi, "max_retries");
        if (mr && cJSON_IsNumber(mr)) {
            const int v = static_cast<int>(mr->valuedouble);
            if (v >= 0 && v <= 255) {
                cfg.wifi.max_retries = static_cast<uint8_t>(v);
            }
        }
    }

    const cJSON* mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (mqtt && cJSON_IsObject(mqtt)) {
        const cJSON* en = cJSON_GetObjectItemCaseSensitive(mqtt, "enabled");
        if (en && (cJSON_IsBool(en) || cJSON_IsNumber(en))) {
            cfg.mqtt.enabled = cJSON_IsTrue(en) || (cJSON_IsNumber(en) && en->valuedouble != 0);
        }
        copy_json_string(cfg.mqtt.host, sizeof(cfg.mqtt.host),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "host"));
        const cJSON* port = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
        if (port && cJSON_IsNumber(port)) {
            const int p = static_cast<int>(port->valuedouble);
            if (p > 0 && p <= 65535) {
                cfg.mqtt.port = static_cast<uint16_t>(p);
            }
        }
        const cJSON* user = cJSON_GetObjectItemCaseSensitive(mqtt, "username");
        if (user && cJSON_IsString(user) && user->valuestring &&
            std::strcmp(user->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.mqtt.username, sizeof(cfg.mqtt.username), user);
        }
        const cJSON* mpw = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
        if (mpw && cJSON_IsString(mpw) && mpw->valuestring &&
            std::strcmp(mpw->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.mqtt.password, sizeof(cfg.mqtt.password), mpw);
        }
        copy_json_string(cfg.mqtt.prefix, sizeof(cfg.mqtt.prefix),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "prefix"));
        copy_json_string(cfg.mqtt.client_id, sizeof(cfg.mqtt.client_id),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "client_id"));
        const cJSON* qos = cJSON_GetObjectItemCaseSensitive(mqtt, "qos");
        if (qos && cJSON_IsNumber(qos)) {
            const int q = static_cast<int>(qos->valuedouble);
            if (q >= 0 && q <= 2) {
                cfg.mqtt.qos = static_cast<uint8_t>(q);
            }
        }
        const cJSON* tls = cJSON_GetObjectItemCaseSensitive(mqtt, "use_tls");
        if (tls && (cJSON_IsBool(tls) || cJSON_IsNumber(tls))) {
            cfg.mqtt.use_tls =
                cJSON_IsTrue(tls) || (cJSON_IsNumber(tls) && tls->valuedouble != 0);
        }
    }

    const cJSON* radio = cJSON_GetObjectItemCaseSensitive(root, "radio");
    if (radio && cJSON_IsObject(radio)) {
        const cJSON* fk = cJSON_GetObjectItemCaseSensitive(radio, "frequency_khz");
        if (fk && cJSON_IsNumber(fk)) {
            cfg.radio.frequency_khz = static_cast<uint32_t>(fk->valuedouble);
        }
        const cJSON* dr = cJSON_GetObjectItemCaseSensitive(radio, "data_rate");
        if (dr && cJSON_IsNumber(dr)) {
            const int v = static_cast<int>(dr->valuedouble);
            if (v >= 0 && v <= 255) {
                cfg.radio.data_rate = static_cast<uint8_t>(v);
            }
        }
        const cJSON* ar = cJSON_GetObjectItemCaseSensitive(radio, "auto_recovery");
        if (ar && (cJSON_IsBool(ar) || cJSON_IsNumber(ar))) {
            cfg.radio.auto_recovery =
                cJSON_IsTrue(ar) || (cJSON_IsNumber(ar) && ar->valuedouble != 0);
        }
    }

    const cJSON* logging = cJSON_GetObjectItemCaseSensitive(root, "logging");
    if (logging && cJSON_IsObject(logging)) {
        const cJSON* lev = cJSON_GetObjectItemCaseSensitive(logging, "level");
        if (lev && cJSON_IsNumber(lev)) {
            const int v = static_cast<int>(lev->valuedouble);
            if (v >= 0 && v <= 255) {
                cfg.logging.level = static_cast<uint8_t>(v);
            }
        }
    }

    const cJSON* auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
    if (auth && cJSON_IsObject(auth)) {
        const cJSON* admin_password = cJSON_GetObjectItemCaseSensitive(auth, "admin_password");
        if (admin_password && cJSON_IsString(admin_password) && admin_password->valuestring &&
            std::strcmp(admin_password->valuestring, config_store::kRedactedValue) != 0 &&
            admin_password->valuestring[0] != '\0') {
            auto hash_result =
                auth_service::AuthService::hash_password(admin_password->valuestring);
            if (hash_result.is_ok()) {
                assign_admin_password_hash(cfg.auth, hash_result.value().c_str());
            }
        }
        const cJSON* st = cJSON_GetObjectItemCaseSensitive(auth, "session_timeout_s");
        if (st && cJSON_IsNumber(st)) {
            cfg.auth.session_timeout_s = static_cast<uint32_t>(st->valuedouble);
        }
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
