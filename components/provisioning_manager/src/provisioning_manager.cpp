#include "provisioning_manager/provisioning_manager.hpp"
#include "config_store/config_store.hpp"
#include "event_bus/event_bus.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include <cstdio>
#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_system.h"
static const char* TAG = "prov_mgr";
#endif

namespace provisioning_manager {

namespace {
static constexpr const char* kProvisioningSsidPrefix = "WMBus-GW-Setup";

void build_provisioning_ap_ssid(char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
#ifndef HOST_TEST_BUILD
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        std::snprintf(out, out_size, "%s-%02X%02X", kProvisioningSsidPrefix, mac[4], mac[5]);
        out[out_size - 1] = '\0';
        return;
    }
    const uint32_t rnd = esp_random();
    std::snprintf(out, out_size, "%s-%04X", kProvisioningSsidPrefix,
                  static_cast<unsigned>(rnd & 0xFFFFu));
    out[out_size - 1] = '\0';
#else
    std::strncpy(out, kProvisioningSsidPrefix, out_size - 1);
    out[out_size - 1] = '\0';
#endif
}
} // namespace

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
    if (config_store::ConfigStore::instance().wifi_is_configured()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Refusing to start provisioning: WiFi is already configured");
#endif
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Starting provisioning mode");
#endif

    char ap_ssid[33] = {};
    build_provisioning_ap_ssid(ap_ssid, sizeof(ap_ssid));

    auto& wifi = wifi_manager::WifiManager::instance();
    auto result = wifi.start_ap(ap_ssid);
    if (result.is_error()) {
        return common::Result<void>::error(common::ErrorCode::WifiApStartFailed);
    }

    state_ = ProvisioningState::Active;

#ifndef HOST_TEST_BUILD
    ESP_LOGW(TAG,
             "Provisioning AP is intentionally open for first-boot setup; keep local access controlled");
    ESP_LOGI(TAG, "Provisioning AP active, SSID: %s", ap_ssid);
#endif

    event_bus::EventBus::instance().publish(event_bus::EventType::ProvisioningStarted);

    return common::Result<void>::ok();
}

common::Result<void> ProvisioningManager::complete() {
    if (state_ != ProvisioningState::Active) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Provisioning completed, config saved");
    ESP_LOGI(TAG, "Provisioning state closed; reboot should be performed to leave AP mode");
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
