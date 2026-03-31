#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "config_store/config_store.hpp"
#include "support_bundle_service/support_bundle_service.hpp"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace api_handlers::detail {

esp_err_t handle_logs(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
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
    for (const auto& entry : entries) {
        cJSON* item = cJSON_CreateObject();
        if (!item) {
            return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
        }
        cJSON_AddNumberToObject(item, "timestamp_us", static_cast<double>(entry.timestamp_us));
        cJSON_AddStringToObject(item, "severity", log_severity_name(entry.severity));
        cJSON_AddStringToObject(item, "message", entry.message.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    return send_json_root(req, 200, root);
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
