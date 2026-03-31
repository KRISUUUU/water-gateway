#include "mdns_service/mdns_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "mdns.h"
static const char* TAG = "mdns_svc";
#endif

namespace mdns_service {

MdnsService& MdnsService::instance() {
    static MdnsService svc;
    return svc;
}

common::Result<void> MdnsService::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    const esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %d", static_cast<int>(err));
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
#endif

    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> MdnsService::start(const char* hostname) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!hostname || hostname[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    esp_err_t err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %d", static_cast<int>(err));
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    err = mdns_instance_name_set("WMBus Gateway");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_instance_name_set failed: %d", static_cast<int>(err));
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_service_add failed: %d", static_cast<int>(err));
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    ESP_LOGI(TAG, "mDNS advertising hostname: %s.local", hostname);
#endif

    started_ = true;
    return common::Result<void>::ok();
}

common::Result<void> MdnsService::stop() {
    if (!started_) {
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    mdns_free();
    initialized_ = false;
    ESP_LOGI(TAG, "mDNS stopped");
#endif

    started_ = false;
    return common::Result<void>::ok();
}

} // namespace mdns_service
