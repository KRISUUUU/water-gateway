#include "api_handlers/api_handlers.hpp"

#include "http_server/http_server.hpp"

#include "auth_service/auth_service.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"
#include "config_store/config_validation.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "support_bundle_service/support_bundle_service.hpp"

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
        case 413:
            status = "413 Payload Too Large";
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
      << "\"max_retries\":" << static_cast<int>(c.wifi.max_retries)
      << "},"
      << "\"mqtt\":{"
      << "\"enabled\":" << (c.mqtt.enabled ? "true" : "false") << ","
      << "\"host\":\"" << json_escape(c.mqtt.host) << "\","
      << "\"port\":" << c.mqtt.port << ","
      << "\"username\":\"" << config_store::kRedactedValue << "\","
      << "\"password\":\"" << config_store::kRedactedValue << "\","
      << "\"prefix\":\"" << json_escape(c.mqtt.prefix) << "\","
      << "\"client_id\":\"" << json_escape(c.mqtt.client_id) << "\","
      << "\"qos\":" << static_cast<int>(c.mqtt.qos) << ","
      << "\"use_tls\":" << (c.mqtt.use_tls ? "true" : "false")
      << "},"
      << "\"radio\":{"
      << "\"frequency_khz\":" << c.radio.frequency_khz << ","
      << "\"data_rate\":" << static_cast<int>(c.radio.data_rate) << ","
      << "\"auto_recovery\":" << (c.radio.auto_recovery ? "true" : "false")
      << "},"
      << "\"logging\":{"
      << "\"level\":" << static_cast<int>(c.logging.level)
      << "},"
      << "\"auth\":{"
      << "\"admin_password_hash\":\"" << config_store::kRedactedValue << "\","
      << "\"session_timeout_s\":" << c.auth.session_timeout_s
      << "}"
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
        return send_json(req, 401, "{\"error\":\"login_failed\"}");
    }
    const auth_service::SessionInfo& s = result.value();
    std::ostringstream o;
    o << "{\"token\":\"" << json_escape(std::string(s.token)) << "\",\"created_epoch_s\":"
      << static_cast<long long>(s.created_epoch_s) << ",\"expires_epoch_s\":"
      << static_cast<long long>(s.expires_epoch_s) << ",\"valid\":true}";
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
    std::ostringstream o;
    o << "{\"health\":{"
      << "\"state\":\""
      << health_monitor::HealthMonitor::state_to_string(health.state) << "\","
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
      << "\"largest_free_block\":" << metrics.largest_free_block
      << "}}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_telegrams(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    return send_json(req, 200, "{\"telegrams\":[]}");
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
      << "\"consecutive_errors\":" << rsm.consecutive_errors()
      << "},"
      << "\"diagnostics\":" << diag_json
      << "}";
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
    config_store::AppConfig cfg = config_store::ConfigStore::instance().config();
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
                issue.severity == config_store::ValidationSeverity::Error ? "error"
                                                                            : "warning");
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
    return send_json(req, 200, "{\"ok\":true}");
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
      << "\"progress_pct\":" << static_cast<int>(st.progress_pct)
      << "}";
    const std::string json = o.str();
    return send_json(req, 200, json.c_str());
}

esp_err_t handle_ota_upload(httpd_req_t* req) {
    esp_err_t g = require_auth(req);
    if (g != ESP_OK) {
        return g;
    }
    // Streamed OTA upload is not yet implemented.
    // The real flow would read multipart chunks and call
    // OtaManager::begin_upload(size) / write_chunk() / finalize_upload().
    return send_json(req, 501,
                     "{\"error\":\"not_implemented\","
                     "\"detail\":\"multipart firmware upload not yet implemented\"}");
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
        return send_json(req, 400, "{\"error\":\"invalid_url_scheme\",\"detail\":\"https_required\"}");
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

}  // namespace

#endif  // !HOST_TEST_BUILD

namespace api_handlers {

void register_all_handlers(void* server) {
#ifndef HOST_TEST_BUILD
    const httpd_handle_t srv = reinterpret_cast<httpd_handle_t>(server);
    if (!srv) {
        ESP_LOGE(TAG, "register_all_handlers: null server");
        return;
    }

#define REG(m, path, fn) \
    if (register_uri(srv, path, m, fn) != ESP_OK) return

    REG(HTTP_POST, "/api/auth/login", handle_auth_login);
    REG(HTTP_POST, "/api/auth/logout", handle_auth_logout);
    REG(HTTP_GET, "/api/status", handle_status);
    REG(HTTP_GET, "/api/telegrams", handle_telegrams);
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

}  // namespace api_handlers
