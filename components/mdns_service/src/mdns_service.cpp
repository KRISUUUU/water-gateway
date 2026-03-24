#include "mdns_service/mdns_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
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
    ESP_LOGW(TAG, "mDNS service is currently running in no-op mode for this build");
    ESP_LOGI(TAG, "mDNS requested hostname: %s.local", hostname);
#endif

    started_ = true;
    return common::Result<void>::ok();
}

common::Result<void> MdnsService::stop() {
    if (!started_) {
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "mDNS no-op stop");
#endif

    started_ = false;
    return common::Result<void>::ok();
}

} // namespace mdns_service
