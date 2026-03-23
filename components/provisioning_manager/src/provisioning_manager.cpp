#include "provisioning_manager/provisioning_manager.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "event_bus/event_bus.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
static const char* TAG = "prov_mgr";
#endif

namespace provisioning_manager {

ProvisioningManager& ProvisioningManager::instance() {
    static ProvisioningManager mgr;
    return mgr;
}

common::Result<void> ProvisioningManager::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> ProvisioningManager::start() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (state_ != ProvisioningState::Idle) {
        return common::Result<void>::error(common::ErrorCode::AlreadyExists);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Starting provisioning mode");
#endif

    auto& wifi = wifi_manager::WifiManager::instance();
    auto result = wifi.start_ap("WMBus-GW-Setup");
    if (result.is_error()) {
        return common::Result<void>::error(common::ErrorCode::WifiApStartFailed);
    }

    state_ = ProvisioningState::Active;

    event_bus::EventBus::instance().publish(
        event_bus::EventType::ProvisioningStarted);

    return common::Result<void>::ok();
}

common::Result<void> ProvisioningManager::complete() {
    if (state_ != ProvisioningState::Active) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Provisioning completed, config saved");
#endif

    state_ = ProvisioningState::Completed;

    event_bus::EventBus::instance().publish(
        event_bus::EventType::ProvisioningCompleted);

    return common::Result<void>::ok();
}

common::Result<void> ProvisioningManager::stop() {
    if (state_ == ProvisioningState::Idle) {
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Stopping provisioning mode");
#endif

    wifi_manager::WifiManager::instance().stop();
    state_ = ProvisioningState::Idle;
    return common::Result<void>::ok();
}

} // namespace provisioning_manager
