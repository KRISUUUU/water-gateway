#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "config_store/config_store.hpp"
#include "support_bundle_service/support_bundle_service.hpp"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

namespace api_handlers::detail {

namespace {
esp_err_t begin_logs_stream(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    apply_json_security_headers(req);
    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_send_chunk(req, "{\"entries\":[", HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_raw_chunk(httpd_req_t* req, const char* data, size_t len) {
    if (!data || len == 0) {
        return ESP_OK;
    }
    return httpd_resp_send_chunk(req, data, static_cast<ssize_t>(len));
}

esp_err_t send_json_escaped_message(httpd_req_t* req, const char* message) {
    if (!message) {
        return ESP_OK;
    }

    char chunk[128];
    size_t used = 0;
    auto flush = [&]() -> esp_err_t {
        const esp_err_t err = send_raw_chunk(req, chunk, used);
        used = 0;
        return err;
    };

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(message); *p != '\0'; ++p) {
        char escaped[7];
        size_t escaped_len = 0;
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = static_cast<char>(*p);
            escaped_len = 2;
        } else if (*p < 0x20) {
            const int n = std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                                        static_cast<unsigned int>(*p));
            if (n < 0) {
                return ESP_FAIL;
            }
            escaped_len = static_cast<size_t>(n);
        } else {
            escaped[0] = static_cast<char>(*p);
            escaped_len = 1;
        }

        if (used + escaped_len > sizeof(chunk)) {
            const esp_err_t err = flush();
            if (err != ESP_OK) {
                return err;
            }
        }
        std::memcpy(chunk + used, escaped, escaped_len);
        used += escaped_len;
    }

    return flush();
}

esp_err_t send_log_entry(httpd_req_t* req, const persistent_log_buffer::LogEntry& entry, bool first) {
    char prefix[128];
    const int prefix_len =
        std::snprintf(prefix, sizeof(prefix), "%s{\"timestamp_us\":%lld,\"severity\":\"%s\",\"message\":\"",
                      first ? "" : ",", static_cast<long long>(entry.timestamp_us),
                      log_severity_name(entry.severity));
    if (prefix_len < 0 || static_cast<size_t>(prefix_len) >= sizeof(prefix)) {
        return ESP_FAIL;
    }

    esp_err_t err = send_raw_chunk(req, prefix, static_cast<size_t>(prefix_len));
    if (err != ESP_OK) {
        return err;
    }
    err = send_json_escaped_message(req, entry.message);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, "\"}", HTTPD_RESP_USE_STRLEN);
}
} // namespace

esp_err_t handle_logs(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    auto& buffer = persistent_log_buffer::PersistentLogBuffer::instance();
    esp_err_t err = begin_logs_stream(req);
    if (err != ESP_OK) {
        return err;
    }

    const std::size_t count = buffer.size();
    bool first = true;
    persistent_log_buffer::LogEntry entry{};
    for (std::size_t i = 0; i < count; ++i) {
        if (!buffer.copy_at(i, entry)) {
            break;
        }
        err = send_log_entry(req, entry, first);
        if (err != ESP_OK) {
            return err;
        }
        first = false;
    }
    return httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
               ? httpd_resp_send_chunk(req, nullptr, 0)
               : ESP_FAIL;
}

esp_err_t handle_support_bundle(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    auto bundle_res =
        support_bundle_service::SupportBundleService::instance().generate_bundle_json();
    return bundle_res.is_ok() ? send_json(req, 200, bundle_res.value().c_str())
                              : send_json(req, 500, "{\"error\":\"support_bundle_failed\"}");
}

esp_err_t handle_system_reboot(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    send_json(req, 200, "{\"ok\":true,\"detail\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_system_factory_reset(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    if (config_store::ConfigStore::instance().reset_to_defaults().is_error()) {
        return send_json(req, 500, "{\"error\":\"reset_failed\"}");
    }
    send_json(req, 200, "{\"ok\":true,\"detail\":\"factory_reset_rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
