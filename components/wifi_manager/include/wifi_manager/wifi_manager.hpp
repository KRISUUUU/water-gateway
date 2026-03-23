#pragma once

#include <string>

#include "common/result.hpp"

namespace wifi_manager {

enum class WifiState {
    Uninitialized = 0,
    Disabled,
    Disconnected,
    Connecting,
    Connected,
    Error
};

struct WifiStatus {
    WifiState state{WifiState::Uninitialized};
    std::string ssid{};
    std::string ip_address{};
    int rssi{0};
};

class WifiManager {
public:
    static WifiManager& instance();

    common::Result<void> initialize();
    common::Result<void> start();
    common::Result<void> stop();
    [[nodiscard]] WifiStatus status() const;

private:
    WifiManager() = default;

    bool initialized_{false};
    WifiStatus status_{};
};

}  // namespace wifi_manager
