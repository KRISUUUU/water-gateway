#include "ota_manager/ota_manager.hpp"

namespace ota_manager {

OtaManager& OtaManager::instance() {
    static OtaManager manager;
    return manager;
}

common::Result<void> OtaManager::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    status_.state = OtaState::Idle;
    status_.message = "OTA idle";

    return common::Result<void>();
}

common::Result<void> OtaManager::begin_from_url(const std::string& url) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    if (url.empty()) {
        return common::Result<void>(common::ErrorCode::InvalidArgument);
    }

    status_.state = OtaState::Downloading;
    status_.message = "Placeholder URL OTA started";
    status_.progress_percent = 0;

    return common::Result<void>();
}

common::Result<void> OtaManager::begin_from_upload() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.state = OtaState::Writing;
    status_.message = "Placeholder upload OTA started";
    status_.progress_percent = 0;

    return common::Result<void>();
}

common::Result<void> OtaManager::mark_boot_successful() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.state = OtaState::Success;
    status_.message = "Boot marked successful";
    status_.progress_percent = 100;

    return common::Result<void>();
}

OtaStatus OtaManager::status() const {
    return status_;
}

}  // namespace ota_manager
