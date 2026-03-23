#include "provisioning_manager/provisioning_manager.hpp"

namespace provisioning_manager {

ProvisioningManager& ProvisioningManager::instance() {
    static ProvisioningManager manager;
    return manager;
}

common::Result<void> ProvisioningManager::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    state_ = ProvisioningState::Idle;
    return common::Result<void>();
}

common::Result<void> ProvisioningManager::start() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    state_ = ProvisioningState::Active;
    return common::Result<void>();
}

common::Result<void> ProvisioningManager::stop() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    state_ = ProvisioningState::Completed;
    return common::Result<void>();
}

ProvisioningState ProvisioningManager::state() const {
    return state_;
}

}  // namespace provisioning_manager
