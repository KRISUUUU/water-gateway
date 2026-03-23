#include "wifi_manager/wifi_manager.hpp"

namespace wifi_manager {

WifiManager& WifiManager::instance() {
    static WifiManager manager;
    return manager;
}

common::Result<void> WifiManager::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    status_.state = WifiState::Disconnected;
    return common::Result<void>();
}

common::Result<void> WifiManager::start() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.state = WifiState::Connecting;

    // Placeholder:
    // Real ESP-IDF Wi-Fi startup logic will be implemented later.
    status_.state = WifiState::Connected;
    status_.ssid = "placeholder-ssid";
    status_.ip_address = "0.0.0.0";
    status_.rssi = 0;

    return common::Result<void>();
}

common::Result<void> WifiManager::stop() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.state = WifiState::Disconnected;
    status_.ssid.clear();
    status_.ip_address.clear();
    status_.rssi = 0;

    return common::Result<void>();
}

WifiStatus WifiManager::status() const {
    return status_;
}

}  // namespace wifi_manager
