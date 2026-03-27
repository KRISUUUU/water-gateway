#include "provisioning_manager/provisioning_manager.hpp"
#include "event_bus/event_bus.hpp"
#include "wifi_manager/wifi_manager.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "esp_wifi.h"
#include <cstdio>
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

#ifndef HOST_TEST_BUILD
    // Derive a device-unique WPA2 password from the lower 4 bytes of the STA MAC.
    // This is printed to UART (serial) only — not stored in logs, config, or support bundles.
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char ap_password[9]; // 8 hex chars + null
    std::snprintf(ap_password, sizeof(ap_password), "%02X%02X%02X%02X",
                  mac[2], mac[3], mac[4], mac[5]);
    // Use printf() rather than ESP_LOGI so the password is not captured by any
    // esp_log_set_vprintf() hook (e.g. remote syslog, in-memory log sinks).
    // Output goes directly to UART via the underlying _write syscall.
    printf("[prov_mgr] Provisioning AP password (UART only): %s\n", ap_password);
    auto result = wifi.start_ap("WMBus-GW-Setup", ap_password);
#else
    auto result = wifi.start_ap("WMBus-GW-Setup");
#endif
    if (result.is_error()) {
        return common::Result<void>::error(common::ErrorCode::WifiApStartFailed);
    }

    state_ = ProvisioningState::Active;

    event_bus::EventBus::instance().publish(event_bus::EventType::ProvisioningStarted);

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

    event_bus::EventBus::instance().publish(event_bus::EventType::ProvisioningCompleted);

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
