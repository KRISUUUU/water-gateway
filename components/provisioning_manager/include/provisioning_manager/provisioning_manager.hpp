#pragma once

#include "common/result.hpp"

namespace provisioning_manager {

enum class ProvisioningState {
    Uninitialized = 0,
    Idle,
    Active,
    Completed,
    Error
};

class ProvisioningManager {
public:
    static ProvisioningManager& instance();

    common::Result<void> initialize();
    common::Result<void> start();
    common::Result<void> stop();

    [[nodiscard]] ProvisioningState state() const;

private:
    ProvisioningManager() = default;

    bool initialized_{false};
    ProvisioningState state_{ProvisioningState::Uninitialized};
};

}  // namespace provisioning_manager
