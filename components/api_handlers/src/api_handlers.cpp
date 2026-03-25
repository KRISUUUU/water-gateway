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
#include <sstream>
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

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o;
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

esp_err_t require_auth(httpd_req_t* req) {
    if (!http_server::HttpServer::instance().authorize_request(req)) {
        return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    }
    return ESP_OK;
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

std::string config_to_json_redacted(const config_store::AppConfig& c) {
    std::ostringstream j;
    j << "{"
      << "\"version\":" << c.version << ","
      << "\"device\":{"
      << "\"name\":\"" << json_escape(c.device.name) << "\","
      << "\"hostname\":\"" << json_escape(c.device.hostname) << "\""
      << "},"
      << "\"wifi\":{"
      << "\"ssid\":\"" << json_escape(c.wifi.ssid) << "\","
      << "\"password\":\"" << config_store::kRedactedValue << "\","
      << "\"max_retries\":" << static_cast<int>(c.wifi.max_retries) << "},"
      << "\"mqtt\":{"
      << "\"enabled\":" << (c.mqtt.enabled ? "true" : "false") << ","
      << "\"host\":\"" << json_escape(c.mqtt.host) << "\","
      << "\"port\":" << c.mqtt.port << ","
      << "\"username\":\"" << config_store::kRedactedValue << "\","
      << "\"password\":\"" << config_store::kRedactedValue << "\","
      << "\"prefix\":\"" << json_escape(c.mqtt.prefix) << "\","
      << "\"client_id\":\"" << json_escape(c.mqtt.client_id) << "\","
      << "\"qos\":" << static_cast<int>(c.mqtt.qos) << ","
      << "\"use_tls\":" << (c.mqtt.use_tls ? "true" : "false") << "},"
      << "\"radio\":{"
      << "\"frequency_khz\":" << c.radio.frequency_khz << ","
      << "\"data_rate\":" << static_cast<int>(c.radio.data_rate) << ","
      << "\"auto_recovery\":" << (c.radio.auto_recovery ? "true" : "false") << "},"
      << "\"logging\":{"
      << "\"level\":" << static_cast<int>(c.logging.level) << "},"
      << "\"auth\":{"
      << "\"admin_password\":\"" << config_store::kRedactedValue << "\","
      << "\"password_set\":" << (c.auth.has_password() ? "true" : "false") << ","
      << "\"session_timeout_s\":" << c.auth.session_timeout_s << "}"
      << "}";
    return j.str();
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
                std::strncpy(cfg.auth.admin_password_hash, hash_result.value().c_str(),
                             sizeof(cfg.auth.admin_password_hash) - 1);
                cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';
            }
        }
        const cJSON* st = cJSON_GetObjectItemCaseSensitive(auth, "session_timeout_s");
        if (st && cJSON_IsNumber(st)) {
            cfg.auth.session_timeout_s = static_cast<uint32_t>(st->valuedouble);
        }
    }
}

esp_err_t handle_auth_login(httpd_req_t* req) {
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
            std::ostringstream o;
            o << "{\"error\":\"rate_limited\",\"retry_after_s\":"
              << static_cast<long long>(retry_after) << "}";
            const std::string json = o.str();
            return send_json(req, 429, json.c_str());
        }
        return send_json(req, 401, "{\"error\":\"login_failed\"}");
    }
    const auth_service::SessionInfo& s = result.value();
    std::ostringstream o;
    o << "{\"token\":\"" << json_escape(std::string(s.token))
      << "\",\"created_epoch_s\":" << static_cast<long long>(s.created_epoch_s)
      << ",\"expires_epoch_s\":" << static_cast<long long>(s.expires_epoch_s) << ",\"valid\":true}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
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
    std::strncpy(updated.auth.admin_password_hash, hash_res.value().c_str(),
                 sizeof(updated.auth.admin_password_hash) - 1);
    updated.auth.admin_password_hash[sizeof(updated.auth.admin_password_hash) - 1] = '\0';

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
    const char* mode = cfg.wifi.is_configured() ? "normal" : "provisioning";
    std::ostringstream o;
    o << "{\"health\":{"
      << "\"state\":\"" << health_monitor::HealthMonitor::state_to_string(health.state) << "\","
      << "\"warning_count\":" << health.warning_count << ","
      << "\"error_count\":" << health.error_count << ","
      << "\"uptime_s\":" << static_cast<unsigned long long>(health.uptime_s) << ","
      << "\"last_warning_msg\":\"" << json_escape(health.last_warning_msg) << "\","
      << "\"last_error_msg\":\"" << json_escape(health.last_error_msg) << "\""
      << "},"
      << "\"metrics\":{"
      << "\"uptime_s\":" << metrics.uptime_s << ","
      << "\"free_heap_bytes\":" << metrics.free_heap_bytes << ","
      << "\"min_free_heap_bytes\":" << metrics.min_free_heap_bytes << ","
      << "\"largest_free_block\":" << metrics.largest_free_block << "},"
      << "\"mode\":\"" << mode << "\","
      << "\"firmware_version\":\"" << json_escape(ota.current_version) << "\","
      << "\"wifi\":{"
      << "\"state\":\"" << wifi_state_name(wifi.state) << "\","
      << "\"ip_address\":\"" << json_escape(wifi.ip_address) << "\","
      << "\"rssi_dbm\":" << static_cast<int>(wifi.rssi_dbm) << ","
      << "\"ssid\":\"" << json_escape(wifi.ssid) << "\","
      << "\"reconnect_count\":" << wifi.reconnect_count << "},"
      << "\"mqtt\":{"
      << "\"state\":\"" << mqtt_state_name(mqtt.state) << "\","
      << "\"broker_uri\":\"" << json_escape(mqtt.broker_uri) << "\","
      << "\"publish_count\":" << mqtt.publish_count << ","
      << "\"publish_failures\":" << mqtt.publish_failures << ","
      << "\"reconnect_count\":" << mqtt.reconnect_count << "},"
      << "\"radio\":{"
      << "\"state\":\"" << radio_state_name(radio.state()) << "\","
      << "\"frames_received\":" << rc.frames_received << ","
      << "\"frames_crc_ok\":" << rc.frames_crc_ok << ","
      << "\"frames_crc_fail\":" << rc.frames_crc_fail << ","
      << "\"frames_incomplete\":" << rc.frames_incomplete << ","
      << "\"frames_dropped_too_long\":" << rc.frames_dropped_too_long << ","
      << "\"fifo_overflows\":" << rc.fifo_overflows << "}"
      << "}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
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
    std::ostringstream o;
    o << "{\"telegrams\":[";
    for (size_t i = 0; i < telegrams.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        const auto& t = telegrams[i];
        o << "{"
          << "\"timestamp_ms\":" << static_cast<long long>(t.timestamp_ms) << ","
          << "\"raw_hex\":\"" << json_escape(t.raw_hex) << "\","
          << "\"frame_length\":" << static_cast<unsigned int>(t.frame_length) << ","
          << "\"rssi_dbm\":" << static_cast<int>(t.rssi_dbm) << ","
          << "\"lqi\":" << static_cast<unsigned int>(t.lqi) << ","
          << "\"crc_ok\":" << (t.crc_ok ? "true" : "false") << ","
          << "\"duplicate\":" << (t.duplicate ? "true" : "false") << ","
          << "\"meter_key\":\"" << json_escape(t.meter_key) << "\","
          << "\"watched\":" << (t.watched ? "true" : "false") << "}";
    }
    o << "]}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_meters_detected(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto meters = meter_registry::MeterRegistry::instance().detected_meters();
    const std::string filter = query_param(req->uri, "filter");
    std::ostringstream o;
    o << "{\"meters\":[";
    size_t emitted = 0;
    for (size_t i = 0; i < meters.size(); ++i) {
        const auto& m = meters[i];
        if (filter == "watched" && !m.watched) {
            continue;
        }
        if (filter == "unknown" && m.watched) {
            continue;
        }
        if (emitted > 0) {
            o << ',';
        }
        emitted++;
        o << "{"
          << "\"key\":\"" << json_escape(m.key) << "\","
          << "\"manufacturer_id\":" << static_cast<unsigned int>(m.manufacturer_id) << ","
          << "\"device_id\":" << static_cast<unsigned long>(m.device_id) << ","
          << "\"first_seen_ms\":" << static_cast<long long>(m.first_seen_ms) << ","
          << "\"last_seen_ms\":" << static_cast<long long>(m.last_seen_ms) << ","
          << "\"seen_count\":" << static_cast<unsigned long>(m.seen_count) << ","
          << "\"last_rssi_dbm\":" << static_cast<int>(m.last_rssi_dbm) << ","
          << "\"last_lqi\":" << static_cast<unsigned int>(m.last_lqi) << ","
          << "\"last_crc_ok\":" << (m.last_crc_ok ? "true" : "false") << ","
          << "\"duplicate_count\":" << static_cast<unsigned long>(m.duplicate_count) << ","
          << "\"crc_fail_count\":" << static_cast<unsigned long>(m.crc_fail_count) << ","
          << "\"watched\":" << (m.watched ? "true" : "false") << ","
          << "\"watch_enabled\":" << (m.watch_enabled ? "true" : "false") << ","
          << "\"alias\":\"" << json_escape(m.alias) << "\","
          << "\"note\":\"" << json_escape(m.note) << "\""
          << "}";
    }
    o << "]}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_watchlist_get(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const auto entries = meter_registry::MeterRegistry::instance().watchlist();
    std::ostringstream o;
    o << "{\"watchlist\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        const auto& e = entries[i];
        o << "{"
          << "\"key\":\"" << json_escape(e.key) << "\","
          << "\"enabled\":" << (e.enabled ? "true" : "false") << ","
          << "\"alias\":\"" << json_escape(e.alias) << "\","
          << "\"note\":\"" << json_escape(e.note) << "\""
          << "}";
    }
    o << "]}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_watchlist_post(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
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
    const std::string diag_json = diagnostics_service::DiagnosticsService::to_json(snap);
    std::ostringstream o;
    o << "{\"rsm\":{"
      << "\"state\":\"" << rsm_state_name(rsm.state()) << "\","
      << "\"consecutive_errors\":" << rsm.consecutive_errors() << "},"
      << "\"diagnostics\":" << diag_json << "}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_diagnostics_mqtt(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const mqtt_service::MqttStatus st = mqtt_service::MqttService::instance().status();
    std::ostringstream o;
    o << "{\"state\":\"" << mqtt_state_name(st.state) << "\","
      << "\"publish_count\":" << st.publish_count << ","
      << "\"publish_failures\":" << st.publish_failures << ","
      << "\"reconnect_count\":" << st.reconnect_count << ","
      << "\"last_publish_epoch_ms\":" << st.last_publish_epoch_ms << ","
      << "\"broker_uri\":\"" << json_escape(st.broker_uri) << "\""
      << "}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
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
        cJSON* err_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(err_root, "ok", false);
        cJSON* issues = cJSON_CreateArray();
        for (const auto& issue : vr.issues) {
            cJSON* it = cJSON_CreateObject();
            cJSON_AddStringToObject(it, "field", issue.field.c_str());
            cJSON_AddStringToObject(it, "message", issue.message.c_str());
            cJSON_AddStringToObject(
                it, "severity",
                issue.severity == config_store::ValidationSeverity::Error ? "error" : "warning");
            cJSON_AddItemToArray(issues, it);
        }
        cJSON_AddItemToObject(err_root, "issues", issues);
        char* printed = cJSON_PrintUnformatted(err_root);
        cJSON_Delete(err_root);
        if (!printed) {
            return send_json(req, 500, "{\"error\":\"internal\"}");
        }
        const esp_err_t e = send_json(req, 400, printed);
        cJSON_free(printed);
        return e;
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

    std::ostringstream ok;
    ok << "{\"ok\":true,"
       << "\"reboot_required\":true,"
       << "\"relogin_required\":" << (auth_changed ? "true" : "false") << ","
       << "\"provisioning_completed\":" << (provisioning_completed ? "true" : "false") << "}";
    const std::string ok_body = ok.str();
    return send_json(req, 200, ok_body.c_str());
}

esp_err_t handle_ota_status(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    const ota_manager::OtaStatus st = ota_manager::OtaManager::instance().status();
    std::ostringstream o;
    o << "{\"state\":\"" << ota_state_name(st.state) << "\","
      << "\"message\":\"" << json_escape(st.message) << "\","
      << "\"progress_pct\":" << static_cast<int>(st.progress_pct) << ","
      << "\"current_version\":\"" << json_escape(st.current_version) << "\""
      << "}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
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
    std::ostringstream o;
    o << "{\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        o << "{\"timestamp_us\":" << entries[i].timestamp_us << ",\"severity\":\""
          << log_severity_name(entries[i].severity) << "\",\"message\":\""
          << json_escape(entries[i].message) << "\"}";
    }
    o << "]}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
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
