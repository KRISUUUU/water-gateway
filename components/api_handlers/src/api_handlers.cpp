#include "api_handlers/api_handlers.hpp"

#include "http_server/http_server.hpp"

#include "auth_service/auth_service.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"
#include "config_store/config_validation.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"
#include "provisioning_manager/provisioning_manager.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "support_bundle_service/support_bundle_service.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#ifndef HOST_TEST_BUILD

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "api_handlers";

namespace {

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

esp_err_t send_json(httpd_req_t* req, int status_code, const char* body);
bool read_request_body(httpd_req_t* req, std::string& out, size_t max_len);
void apply_config_json(const cJSON* root, config_store::AppConfig& cfg);
bool request_content_type_is_json(httpd_req_t* req);

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

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

bool has_https_scheme(const std::string& url) {
    static constexpr const char* kHttps = "https://";
    return url.size() >= 8 && url.compare(0, 8, kHttps) == 0;
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

esp_err_t send_json(httpd_req_t* req, int status_code, const char* body) {
    httpd_resp_set_type(req, "application/json");
    // Harden API responses against caching and content-type confusion.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
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

bool request_content_type_is_json(httpd_req_t* req) {
    const size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        // Keep backward compatibility for existing clients that omit Content-Type.
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

esp_err_t require_auth(httpd_req_t* req) {
    if (!http_server::HttpServer::instance().authorize_request(req)) {
        return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    }
    return ESP_OK;
}

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
    const bool provisioning = !current_cfg.wifi.is_configured();
    const bool password_set = current_cfg.auth.has_password();
    if (!provisioning || password_set) {
        return send_json(
            req, 409,
            "{\"error\":\"bootstrap_setup_not_allowed\",\"detail\":\"already_configured\"}");
    }

    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }

    std::string body;
    if (!read_request_body(req, body, 16384)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    config_store::AppConfig cfg = current_cfg;
    apply_config_json(root, cfg);
    cJSON_Delete(root);

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
        JsonPtr err_root = make_json_object();
        if (!err_root) {
            return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
        }
        cJSON_AddBoolToObject(err_root.get(), "ok", false);
        cJSON* issues = cJSON_AddArrayToObject(err_root.get(), "issues");
        if (!issues) {
            return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
        }
        for (const auto& issue : save_result.value().issues) {
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

    auto& prov = provisioning_manager::ProvisioningManager::instance();
    bool provisioning_completed = false;
    if (prov.is_active()) {
        auto complete_result = prov.complete();
        provisioning_completed = complete_result.is_ok();
    }

    JsonPtr ok = make_json_object();
    if (!ok) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddBoolToObject(ok.get(), "ok", true);
    cJSON_AddBoolToObject(ok.get(), "reboot_required", true);
    cJSON_AddBoolToObject(ok.get(), "provisioning_completed", provisioning_completed);
    return send_json_root(req, 200, ok);
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

bool request_content_type_is_binary(httpd_req_t* req) {
    const size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        return true; // treat missing header as acceptable for raw upload
    }
    std::string value;
    value.resize(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, "Content-Type", value.data(), value.size()) != ESP_OK) {
        return false;
    }
    const std::string lower = [&value]() {
        std::string tmp = value.c_str();
        for (char& c : tmp) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return tmp;
    }();
    return lower.find("application/octet-stream") != std::string::npos ||
           lower.find("application/x-binary") != std::string::npos;
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

bool copy_json_string(char* dest, size_t dest_sz, const cJSON* item) {
    if (!dest || dest_sz == 0 || !item || !cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    std::strncpy(dest, item->valuestring, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
    return true;
}

void apply_config_json(const cJSON* root, config_store::AppConfig& cfg) {
    // cfg.version is managed by the config system, not by API clients.

    const cJSON* device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (device && cJSON_IsObject(device)) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(device, "name");
        copy_json_string(cfg.device.name, sizeof(cfg.device.name), name);
        const cJSON* host = cJSON_GetObjectItemCaseSensitive(device, "hostname");
        copy_json_string(cfg.device.hostname, sizeof(cfg.device.hostname), host);
    }

    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (wifi && cJSON_IsObject(wifi)) {
        const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        copy_json_string(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), ssid);
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
        const cJSON* host = cJSON_GetObjectItemCaseSensitive(mqtt, "host");
        copy_json_string(cfg.mqtt.host, sizeof(cfg.mqtt.host), host);
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
        const cJSON* pref = cJSON_GetObjectItemCaseSensitive(mqtt, "prefix");
        copy_json_string(cfg.mqtt.prefix, sizeof(cfg.mqtt.prefix), pref);
        const cJSON* cid = cJSON_GetObjectItemCaseSensitive(mqtt, "client_id");
        copy_json_string(cfg.mqtt.client_id, sizeof(cfg.mqtt.client_id), cid);
        const cJSON* qos = cJSON_GetObjectItemCaseSensitive(mqtt, "qos");
        if (qos && cJSON_IsNumber(qos)) {
            const int q = static_cast<int>(qos->valuedouble);
            if (q >= 0 && q <= 2) {
                cfg.mqtt.qos = static_cast<uint8_t>(q);
            }
        }
        const cJSON* tls = cJSON_GetObjectItemCaseSensitive(mqtt, "use_tls");
        if (tls && (cJSON_IsBool(tls) || cJSON_IsNumber(tls))) {
            cfg.mqtt.use_tls = cJSON_IsTrue(tls) || (cJSON_IsNumber(tls) && tls->valuedouble != 0);
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

esp_err_t handle_auth_login(httpd_req_t* req) {
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }
    cJSON* pw = cJSON_GetObjectItemCaseSensitive(root, "password");
    const char* password = (pw && cJSON_IsString(pw)) ? pw->valuestring : nullptr;
    auto result = auth_service::AuthService::instance().login(password);
    cJSON_Delete(root);
    if (result.is_error()) {
        if (result.error() == common::ErrorCode::AuthRateLimited) {
            const int32_t retry_after = auth_service::AuthService::instance().retry_after_seconds();
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
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    auto r = auth_service::AuthService::instance().logout();
    if (r.is_error()) {
        return send_json(req, 500, "{\"error\":\"logout_failed\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t handle_auth_password(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    const cJSON* current = cJSON_GetObjectItemCaseSensitive(root, "current_password");
    const cJSON* next = cJSON_GetObjectItemCaseSensitive(root, "new_password");
    const char* current_password =
        (current && cJSON_IsString(current)) ? current->valuestring : nullptr;
    const char* new_password = (next && cJSON_IsString(next)) ? next->valuestring : nullptr;

    if (!new_password || new_password[0] == '\0' ||
        std::strlen(new_password) > auth_service::kMaxPasswordLength) {
        cJSON_Delete(root);
        return send_json(req, 400, "{\"error\":\"invalid_new_password\"}");
    }

    const auto cfg = config_store::ConfigStore::instance().config();
    if (cfg.auth.has_password() && !auth_service::AuthService::verify_password(
                                       current_password, cfg.auth.admin_password_hash)) {
        cJSON_Delete(root);
        return send_json(req, 401, "{\"error\":\"current_password_invalid\"}");
    }

    auto hash_res = auth_service::AuthService::hash_password(new_password);
    if (hash_res.is_error()) {
        cJSON_Delete(root);
        return send_json(req, 500, "{\"error\":\"password_hash_failed\"}");
    }
    cJSON_Delete(root);

    config_store::AppConfig updated = cfg;
    assign_admin_password_hash(updated.auth, hash_res.value().c_str());

    auto save = config_store::ConfigStore::instance().save(updated);
    if (save.is_error()) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    if (!save.value().valid) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    (void)auth_service::AuthService::instance().logout();
    return send_json(req, 200, "{\"ok\":true,\"relogin_required\":true}");
}

esp_err_t handle_status(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"health_snapshot_failed\"}");
    }
    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"metrics_snapshot_failed\"}");
    }
    const auto& health = health_res.value();
    const auto& metrics = metrics_res.value();
    const auto cfg = config_store::ConfigStore::instance().config();
    const auto wifi = wifi_manager::WifiManager::instance().status();
    const auto mqtt = mqtt_service::MqttService::instance().status();
    const auto& radio = radio_cc1101::RadioCc1101::instance();
    const auto& rc = radio.counters();
    const auto ota = ota_manager::OtaManager::instance().status();
    const auto cfg_runtime = config_store::ConfigStore::instance().runtime_status();
    const char* mode = cfg.wifi.is_configured() ? "normal" : "provisioning";
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON* health_o = cJSON_AddObjectToObject(root.get(), "health");
    cJSON_AddStringToObject(health_o, "state",
                            health_monitor::HealthMonitor::state_to_string(health.state));
    cJSON_AddNumberToObject(health_o, "warning_count", static_cast<double>(health.warning_count));
    cJSON_AddNumberToObject(health_o, "error_count", static_cast<double>(health.error_count));
    cJSON_AddNumberToObject(health_o, "uptime_s", static_cast<double>(health.uptime_s));
    cJSON_AddStringToObject(health_o, "last_warning_msg", health.last_warning_msg.c_str());
    cJSON_AddStringToObject(health_o, "last_error_msg", health.last_error_msg.c_str());

    cJSON* metrics_o = cJSON_AddObjectToObject(root.get(), "metrics");
    cJSON_AddNumberToObject(metrics_o, "uptime_s", static_cast<double>(metrics.uptime_s));
    cJSON_AddNumberToObject(metrics_o, "free_heap_bytes",
                            static_cast<double>(metrics.free_heap_bytes));
    cJSON_AddNumberToObject(metrics_o, "min_free_heap_bytes",
                            static_cast<double>(metrics.min_free_heap_bytes));
    cJSON_AddNumberToObject(metrics_o, "largest_free_block",
                            static_cast<double>(metrics.largest_free_block));
    cJSON* tasks_o = cJSON_AddObjectToObject(metrics_o, "tasks");
    cJSON_AddNumberToObject(tasks_o, "radio_loop_age_ms",
                            static_cast<double>(metrics.tasks.radio_loop_age_ms));
    cJSON_AddNumberToObject(tasks_o, "pipeline_loop_age_ms",
                            static_cast<double>(metrics.tasks.pipeline_loop_age_ms));
    cJSON_AddNumberToObject(tasks_o, "mqtt_loop_age_ms",
                            static_cast<double>(metrics.tasks.mqtt_loop_age_ms));
    cJSON_AddNumberToObject(tasks_o, "pipeline_frames_processed",
                            static_cast<double>(metrics.tasks.pipeline_frames_processed));
    cJSON_AddNumberToObject(tasks_o, "radio_stall_count",
                            static_cast<double>(metrics.tasks.radio_stall_count));
    cJSON_AddNumberToObject(tasks_o, "pipeline_stall_count",
                            static_cast<double>(metrics.tasks.pipeline_stall_count));
    cJSON_AddNumberToObject(tasks_o, "mqtt_stall_count",
                            static_cast<double>(metrics.tasks.mqtt_stall_count));
    cJSON_AddNumberToObject(tasks_o, "watchdog_register_errors",
                            static_cast<double>(metrics.tasks.watchdog_register_errors));
    cJSON_AddNumberToObject(tasks_o, "watchdog_feed_errors",
                            static_cast<double>(metrics.tasks.watchdog_feed_errors));

    cJSON_AddStringToObject(root.get(), "mode", mode);
    cJSON_AddStringToObject(root.get(), "firmware_version", ota.current_version);
    cJSON* security_o = cJSON_AddObjectToObject(root.get(), "security");
    cJSON_AddBoolToObject(security_o, "admin_password_set", cfg.auth.has_password());
    cJSON_AddBoolToObject(security_o, "provisioning_ap_open", !cfg.wifi.is_configured());
    cJSON_AddBoolToObject(security_o, "bootstrap_login_open",
                          !cfg.wifi.is_configured() && !cfg.auth.has_password());
    cJSON* config_o = cJSON_AddObjectToObject(root.get(), "config_store");
    cJSON_AddStringToObject(config_o, "load_source", config_load_source_name(cfg_runtime.load_source));
    cJSON_AddBoolToObject(config_o, "used_defaults", cfg_runtime.used_defaults);
    cJSON_AddBoolToObject(config_o, "loaded_from_backup", cfg_runtime.loaded_from_backup);
    cJSON_AddBoolToObject(config_o, "defaults_persisted", cfg_runtime.defaults_persisted);
    cJSON_AddBoolToObject(config_o, "defaults_persist_deferred",
                          cfg_runtime.defaults_persist_deferred);
    cJSON_AddStringToObject(config_o, "last_load_error",
                            common::error_code_to_string(cfg_runtime.last_load_error));
    cJSON_AddStringToObject(config_o, "last_persist_error",
                            common::error_code_to_string(cfg_runtime.last_persist_error));
    cJSON_AddStringToObject(config_o, "last_migration_error",
                            common::error_code_to_string(cfg_runtime.last_migration_error));
    cJSON_AddNumberToObject(config_o, "load_attempts", static_cast<double>(cfg_runtime.load_attempts));
    cJSON_AddNumberToObject(config_o, "load_failures", static_cast<double>(cfg_runtime.load_failures));
    cJSON_AddNumberToObject(config_o, "primary_read_failures",
                            static_cast<double>(cfg_runtime.primary_read_failures));
    cJSON_AddNumberToObject(config_o, "backup_read_failures",
                            static_cast<double>(cfg_runtime.backup_read_failures));
    cJSON_AddNumberToObject(config_o, "validation_failures",
                            static_cast<double>(cfg_runtime.validation_failures));
    cJSON_AddNumberToObject(config_o, "migration_attempts",
                            static_cast<double>(cfg_runtime.migration_attempts));
    cJSON_AddNumberToObject(config_o, "migration_failures",
                            static_cast<double>(cfg_runtime.migration_failures));
    cJSON_AddNumberToObject(config_o, "save_attempts", static_cast<double>(cfg_runtime.save_attempts));
    cJSON_AddNumberToObject(config_o, "save_successes", static_cast<double>(cfg_runtime.save_successes));
    cJSON_AddNumberToObject(config_o, "save_failures", static_cast<double>(cfg_runtime.save_failures));
    cJSON_AddNumberToObject(config_o, "save_validation_rejects",
                            static_cast<double>(cfg_runtime.save_validation_rejects));

    cJSON* wifi_o = cJSON_AddObjectToObject(root.get(), "wifi");
    cJSON_AddStringToObject(wifi_o, "state", wifi_state_name(wifi.state));
    cJSON_AddStringToObject(wifi_o, "ip_address", wifi.ip_address);
    cJSON_AddNumberToObject(wifi_o, "rssi_dbm", static_cast<double>(wifi.rssi_dbm));
    cJSON_AddStringToObject(wifi_o, "ssid", wifi.ssid);
    cJSON_AddNumberToObject(wifi_o, "reconnect_count", static_cast<double>(wifi.reconnect_count));

    cJSON* mqtt_o = cJSON_AddObjectToObject(root.get(), "mqtt");
    cJSON_AddStringToObject(mqtt_o, "state", mqtt_state_name(mqtt.state));
    cJSON_AddStringToObject(mqtt_o, "broker_uri", mqtt.broker_uri);
    cJSON_AddNumberToObject(mqtt_o, "publish_count", static_cast<double>(mqtt.publish_count));
    cJSON_AddNumberToObject(mqtt_o, "publish_failures", static_cast<double>(mqtt.publish_failures));
    cJSON_AddNumberToObject(mqtt_o, "reconnect_count", static_cast<double>(mqtt.reconnect_count));

    cJSON* radio_o = cJSON_AddObjectToObject(root.get(), "radio");
    cJSON_AddStringToObject(radio_o, "state", radio_state_name(radio.state()));
    cJSON_AddNumberToObject(radio_o, "frames_received", static_cast<double>(rc.frames_received));
    cJSON_AddNumberToObject(radio_o, "frames_crc_ok", static_cast<double>(rc.frames_crc_ok));
    cJSON_AddNumberToObject(radio_o, "frames_crc_fail", static_cast<double>(rc.frames_crc_fail));
    cJSON_AddNumberToObject(radio_o, "frames_incomplete",
                            static_cast<double>(rc.frames_incomplete));
    cJSON_AddNumberToObject(radio_o, "frames_dropped_too_long",
                            static_cast<double>(rc.frames_dropped_too_long));
    cJSON_AddNumberToObject(radio_o, "fifo_overflows", static_cast<double>(rc.fifo_overflows));

    cJSON* queues_o = cJSON_AddObjectToObject(root.get(), "queues");
    cJSON* frame_q_o = cJSON_AddObjectToObject(queues_o, "frame_queue");
    cJSON_AddNumberToObject(frame_q_o, "depth",
                            static_cast<double>(metrics.queues.frame_queue_depth));
    cJSON_AddNumberToObject(frame_q_o, "peak_depth",
                            static_cast<double>(metrics.queues.frame_queue_peak_depth));
    cJSON_AddNumberToObject(frame_q_o, "enqueue_success",
                            static_cast<double>(metrics.queues.frame_enqueue_success));
    cJSON_AddNumberToObject(frame_q_o, "enqueue_drop",
                            static_cast<double>(metrics.queues.frame_enqueue_drop));
    cJSON_AddNumberToObject(frame_q_o, "enqueue_errors",
                            static_cast<double>(metrics.queues.frame_enqueue_errors));

    cJSON* outbox_o = cJSON_AddObjectToObject(queues_o, "mqtt_outbox");
    cJSON_AddNumberToObject(outbox_o, "depth",
                            static_cast<double>(metrics.queues.mqtt_outbox_depth));
    cJSON_AddNumberToObject(outbox_o, "peak_depth",
                            static_cast<double>(metrics.queues.mqtt_outbox_peak_depth));
    cJSON_AddNumberToObject(outbox_o, "enqueue_success",
                            static_cast<double>(metrics.queues.mqtt_outbox_enqueue_success));
    cJSON_AddNumberToObject(outbox_o, "enqueue_drop",
                            static_cast<double>(metrics.queues.mqtt_outbox_enqueue_drop));
    cJSON_AddNumberToObject(outbox_o, "enqueue_errors",
                            static_cast<double>(metrics.queues.mqtt_outbox_enqueue_errors));
    return send_json_root(req, 200, root);
}

esp_err_t handle_telegrams(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    meter_registry::TelegramFilter filter = meter_registry::TelegramFilter::All;
    const std::string f = query_param(req->uri, "filter");
    if (f == "watched") {
        filter = meter_registry::TelegramFilter::WatchedOnly;
    } else if (f == "unknown") {
        filter = meter_registry::TelegramFilter::UnknownOnly;
    } else if (f == "duplicates") {
        filter = meter_registry::TelegramFilter::DuplicatesOnly;
    } else if (f == "crc_fail") {
        filter = meter_registry::TelegramFilter::CrcFailOnly;
    }

    const auto telegrams = meter_registry::MeterRegistry::instance().recent_telegrams(filter);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    esp_err_t e = httpd_resp_send_chunk(req, "{\"telegrams\":[", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }

    bool first = true;
    std::string row;
    for (const auto& t : telegrams) {
        row.clear();
        row.reserve(256 + t.raw_hex.size() + t.meter_key.size());
        if (!first) {
            row.push_back(',');
        }
        first = false;
        row += "{\"timestamp_ms\":";
        row += std::to_string(static_cast<long long>(t.timestamp_ms));
        row += ",\"raw_hex\":\"";
        if (is_hex_string(t.raw_hex)) {
            row += t.raw_hex;
        } else {
            json_escape_append(row, t.raw_hex);
        }
        row += "\",\"frame_length\":";
        row += std::to_string(static_cast<unsigned int>(t.frame_length));
        row += ",\"rssi_dbm\":";
        row += std::to_string(static_cast<int>(t.rssi_dbm));
        row += ",\"lqi\":";
        row += std::to_string(static_cast<unsigned int>(t.lqi));
        row += ",\"crc_ok\":";
        row += t.crc_ok ? "true" : "false";
        row += ",\"duplicate\":";
        row += t.duplicate ? "true" : "false";
        row += ",\"meter_key\":\"";
        json_escape_append(row, t.meter_key);
        row += "\",\"watched\":";
        row += t.watched ? "true" : "false";
        row += '}';
        e = send_json_chunk_row(req, row);
        if (e != ESP_OK) {
            return e;
        }
    }

    e = httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_meters_detected(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto meters = meter_registry::MeterRegistry::instance().detected_meters();
    const std::string filter = query_param(req->uri, "filter");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    esp_err_t e = httpd_resp_send_chunk(req, "{\"meters\":[", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }

    bool first = true;
    std::string row;
    for (const auto& m : meters) {
        if (filter == "watched" && !m.watched) {
            continue;
        }
        if (filter == "unknown" && m.watched) {
            continue;
        }
        row.clear();
        row.reserve(256 + m.key.size() + m.alias.size() + m.note.size());
        if (!first) {
            row.push_back(',');
        }
        first = false;
        row += "{\"key\":\"";
        json_escape_append(row, m.key);
        row += "\",\"manufacturer_id\":";
        row += std::to_string(static_cast<unsigned int>(m.manufacturer_id));
        row += ",\"device_id\":";
        row += std::to_string(static_cast<unsigned long long>(m.device_id));
        row += ",\"first_seen_ms\":";
        row += std::to_string(static_cast<long long>(m.first_seen_ms));
        row += ",\"last_seen_ms\":";
        row += std::to_string(static_cast<long long>(m.last_seen_ms));
        row += ",\"seen_count\":";
        row += std::to_string(static_cast<unsigned long long>(m.seen_count));
        row += ",\"last_rssi_dbm\":";
        row += std::to_string(static_cast<int>(m.last_rssi_dbm));
        row += ",\"last_lqi\":";
        row += std::to_string(static_cast<unsigned int>(m.last_lqi));
        row += ",\"last_crc_ok\":";
        row += m.last_crc_ok ? "true" : "false";
        row += ",\"duplicate_count\":";
        row += std::to_string(static_cast<unsigned long long>(m.duplicate_count));
        row += ",\"crc_fail_count\":";
        row += std::to_string(static_cast<unsigned long long>(m.crc_fail_count));
        row += ",\"watched\":";
        row += m.watched ? "true" : "false";
        row += ",\"watch_enabled\":";
        row += m.watch_enabled ? "true" : "false";
        row += ",\"alias\":\"";
        json_escape_append(row, m.alias);
        row += "\",\"note\":\"";
        json_escape_append(row, m.note);
        row += "\"}";
        e = send_json_chunk_row(req, row);
        if (e != ESP_OK) {
            return e;
        }
    }

    e = httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_watchlist_get(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto entries = meter_registry::MeterRegistry::instance().watchlist();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    esp_err_t e = httpd_resp_send_chunk(req, "{\"watchlist\":[", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }

    bool first = true;
    std::string row;
    for (const auto& ent : entries) {
        row.clear();
        row.reserve(128 + ent.key.size() + ent.alias.size() + ent.note.size());
        if (!first) {
            row.push_back(',');
        }
        first = false;
        row += "{\"key\":\"";
        json_escape_append(row, ent.key);
        row += "\",\"enabled\":";
        row += ent.enabled ? "true" : "false";
        row += ",\"alias\":\"";
        json_escape_append(row, ent.alias);
        row += "\",\"note\":\"";
        json_escape_append(row, ent.note);
        row += "\"}";
        e = send_json_chunk_row(req, row);
        if (e != ESP_OK) {
            return e;
        }
    }

    e = httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    if (e != ESP_OK) {
        return e;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_watchlist_post(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    meter_registry::WatchlistEntry e{};
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root, "key");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    const cJSON* alias = cJSON_GetObjectItemCaseSensitive(root, "alias");
    const cJSON* note = cJSON_GetObjectItemCaseSensitive(root, "note");

    if (key && cJSON_IsString(key) && key->valuestring) {
        e.key = key->valuestring;
    }
    if (enabled && (cJSON_IsBool(enabled) || cJSON_IsNumber(enabled))) {
        e.enabled = cJSON_IsTrue(enabled) || (cJSON_IsNumber(enabled) && enabled->valuedouble != 0);
    }
    if (alias && cJSON_IsString(alias) && alias->valuestring) {
        e.alias = alias->valuestring;
    }
    if (note && cJSON_IsString(note) && note->valuestring) {
        e.note = note->valuestring;
    }
    cJSON_Delete(root);

    if (e.key.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_key\"}");
    }
    auto r = meter_registry::MeterRegistry::instance().upsert_watchlist(e);
    if (r.is_error()) {
        return send_json(req, 500, "{\"error\":\"watchlist_save_failed\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t handle_watchlist_delete(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 2048)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root, "key");
    std::string key_val = (key && cJSON_IsString(key) && key->valuestring) ? key->valuestring : "";
    cJSON_Delete(root);
    if (key_val.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_key\"}");
    }
    auto r = meter_registry::MeterRegistry::instance().remove_watchlist(key_val);
    if (r.is_error()) {
        return send_json(req, 500, "{\"error\":\"watchlist_delete_failed\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t handle_diagnostics_radio(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto& rsm = radio_state_machine::RadioStateMachine::instance();
    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    if (snap_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"diagnostics_snapshot_failed\"}");
    }
    const auto& snap = snap_res.value();
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
    const std::string diag_json = diagnostics_service::DiagnosticsService::to_json(snap);
    cJSON* diag_obj = cJSON_Parse(diag_json.c_str());
    if (!diag_obj) {
        return send_json(req, 500, "{\"error\":\"diagnostics_json_invalid\"}");
    }
    cJSON_AddItemToObject(root.get(), "diagnostics", diag_obj);
    return send_json_root(req, 200, root);
}

esp_err_t handle_diagnostics_mqtt(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const mqtt_service::MqttStatus st = mqtt_service::MqttService::instance().status();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddStringToObject(root.get(), "state", mqtt_state_name(st.state));
    cJSON_AddNumberToObject(root.get(), "publish_count", static_cast<double>(st.publish_count));
    cJSON_AddNumberToObject(root.get(), "publish_failures",
                            static_cast<double>(st.publish_failures));
    cJSON_AddNumberToObject(root.get(), "reconnect_count", static_cast<double>(st.reconnect_count));
    cJSON_AddNumberToObject(root.get(), "last_publish_epoch_ms",
                            static_cast<double>(st.last_publish_epoch_ms));
    cJSON_AddStringToObject(root.get(), "broker_uri", st.broker_uri);
    return send_json_root(req, 200, root);
}

esp_err_t handle_config_get(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const config_store::AppConfig cfg = config_store::ConfigStore::instance().config();
    const std::string json = config_to_json_redacted(cfg);
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_config_post(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 32768)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }
    const config_store::AppConfig previous_cfg = config_store::ConfigStore::instance().config();
    config_store::AppConfig cfg = previous_cfg;
    apply_config_json(root, cfg);
    cJSON_Delete(root);

    auto save_result = config_store::ConfigStore::instance().save(cfg);
    if (save_result.is_error()) {
        return send_json(req, 500, "{\"error\":\"save_failed\"}");
    }
    const config_store::ValidationResult& vr = save_result.value();
    if (!vr.valid) {
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

    bool provisioning_completed = false;
    auto& prov = provisioning_manager::ProvisioningManager::instance();
    if (prov.is_active() && cfg.wifi.is_configured()) {
        auto complete_result = prov.complete();
        provisioning_completed = complete_result.is_ok();
    }
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

esp_err_t handle_ota_status(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const ota_manager::OtaStatus st = ota_manager::OtaManager::instance().status();
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
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
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
        const auto ec = begin.error();
        if (ec == common::ErrorCode::OtaAlreadyInProgress) {
            return send_json(req, 409, "{\"error\":\"ota_in_progress\"}");
        }
        if (ec == common::ErrorCode::OtaImageTooLarge) {
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
        const int r = httpd_req_recv(req, chunk, to_read);
        if (r <= 0) {
            return send_json(req, 500, "{\"error\":\"upload_read_failed\"}");
        }

        auto wr = ota_manager::OtaManager::instance().write_chunk(
            reinterpret_cast<const uint8_t*>(chunk), static_cast<size_t>(r));
        if (wr.is_error()) {
            return send_json(req, 500, "{\"error\":\"ota_write_failed\"}");
        }
        remaining -= r;
    }

    auto fin = ota_manager::OtaManager::instance().finalize_upload();
    if (fin.is_error()) {
        return send_json(req, 500, "{\"error\":\"ota_finalize_failed\"}");
    }

    return send_json(req, 200,
                     "{\"ok\":true,\"reboot_required\":true,"
                     "\"detail\":\"ota_upload_complete_reboot_to_activate\"}");
}

esp_err_t handle_ota_url(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415, "{\"error\":\"unsupported_content_type\",\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }
    cJSON* url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const char* surl = (url && cJSON_IsString(url)) ? url->valuestring : nullptr;
    std::string url_copy = surl ? surl : "";
    cJSON_Delete(root);

    if (url_copy.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_url\"}");
    }
    if (!has_https_scheme(url_copy)) {
        return send_json(req, 400,
                         "{\"error\":\"invalid_url_scheme\",\"detail\":\"https_required\"}");
    }
    auto r = ota_manager::OtaManager::instance().begin_url_ota(url_copy.c_str());
    if (r.is_error()) {
        return send_json(req, 400, "{\"error\":\"ota_begin_failed\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

esp_err_t handle_logs(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto entries = persistent_log_buffer::PersistentLogBuffer::instance().lines();
    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    cJSON* arr = cJSON_AddArrayToObject(root.get(), "entries");
    if (!arr) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }
    for (const auto& e : entries) {
        cJSON* item = cJSON_CreateObject();
        if (!item) {
            return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
        }
        cJSON_AddNumberToObject(item, "timestamp_us", static_cast<double>(e.timestamp_us));
        cJSON_AddStringToObject(item, "severity", log_severity_name(e.severity));
        cJSON_AddStringToObject(item, "message", e.message.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    return send_json_root(req, 200, root);
}

esp_err_t handle_support_bundle(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    auto bundle_res =
        support_bundle_service::SupportBundleService::instance().generate_bundle_json();
    if (bundle_res.is_error()) {
        return send_json(req, 500, "{\"error\":\"support_bundle_failed\"}");
    }
    const std::string& bundle = bundle_res.value();
    return send_json(req, 200, bundle.c_str());
}

esp_err_t handle_system_reboot(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    send_json(req, 200, "{\"ok\":true,\"detail\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_system_factory_reset(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    auto r = config_store::ConfigStore::instance().reset_to_defaults();
    if (r.is_error()) {
        return send_json(req, 500, "{\"error\":\"reset_failed\"}");
    }
    send_json(req, 200, "{\"ok\":true,\"detail\":\"factory_reset_rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

esp_err_t register_uri(httpd_handle_t server, const char* uri, httpd_method_t method,
                       esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t u{};
    u.uri = uri;
    u.method = method;
    u.handler = handler;
    u.user_ctx = nullptr;
    const esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s: %d", uri, err);
        return err;
    }
    return ESP_OK;
}

} // namespace

#endif // !HOST_TEST_BUILD

namespace api_handlers {

void register_all_handlers(void* server) {
#ifndef HOST_TEST_BUILD
    const httpd_handle_t srv = reinterpret_cast<httpd_handle_t>(server);
    if (!srv) {
        ESP_LOGE(TAG, "register_all_handlers: null server");
        return;
    }

#define REG(m, path, fn)                                                                           \
    if (register_uri(srv, path, m, fn) != ESP_OK)                                                  \
    return

    REG(HTTP_GET, "/api/bootstrap", handle_bootstrap);
    REG(HTTP_POST, "/api/bootstrap/setup", handle_bootstrap_setup);
    REG(HTTP_POST, "/api/auth/login", handle_auth_login);
    REG(HTTP_POST, "/api/auth/logout", handle_auth_logout);
    REG(HTTP_POST, "/api/auth/password", handle_auth_password);
    REG(HTTP_GET, "/api/status", handle_status);
    REG(HTTP_GET, "/api/telegrams", handle_telegrams);
    REG(HTTP_GET, "/api/meters/detected", handle_meters_detected);
    REG(HTTP_GET, "/api/watchlist", handle_watchlist_get);
    REG(HTTP_POST, "/api/watchlist", handle_watchlist_post);
    REG(HTTP_POST, "/api/watchlist/delete", handle_watchlist_delete);
    REG(HTTP_GET, "/api/diagnostics/radio", handle_diagnostics_radio);
    REG(HTTP_GET, "/api/diagnostics/mqtt", handle_diagnostics_mqtt);
    REG(HTTP_GET, "/api/config", handle_config_get);
    REG(HTTP_POST, "/api/config", handle_config_post);
    REG(HTTP_GET, "/api/ota/status", handle_ota_status);
    REG(HTTP_POST, "/api/ota/upload", handle_ota_upload);
    REG(HTTP_POST, "/api/ota/url", handle_ota_url);
    REG(HTTP_GET, "/api/logs", handle_logs);
    REG(HTTP_GET, "/api/support-bundle", handle_support_bundle);
    REG(HTTP_POST, "/api/system/reboot", handle_system_reboot);
    REG(HTTP_POST, "/api/system/factory-reset", handle_system_factory_reset);

#undef REG
#else
    (void)server;
#endif
}

} // namespace api_handlers
