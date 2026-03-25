#pragma once

#include "common/result.hpp"
#include <cstdint>

namespace provisioning_manager {

enum class ProvisioningState : uint8_t {
    Idle = 0,
    Active,    // AP running, serving config form
    Completed, // Config saved, pending reboot
};

class ProvisioningManager {
  public:
    static ProvisioningManager& instance();

    common::Result<void> initialize();

    // Start provisioning mode: starts WiFi AP and serves provisioning page
    common::Result<void> start();

    // Called when provisioning form is submitted and config saved
    common::Result<void> complete();

    // Stop provisioning (cleanup AP)
    common::Result<void> stop();

    ProvisioningState state() const {
        return state_;
    }
    bool is_active() const {
        return state_ == ProvisioningState::Active;
    }

  private:
    ProvisioningManager() = default;

    bool initialized_ = false;
    ProvisioningState state_ = ProvisioningState::Idle;
};

} // namespace provisioning_manager
