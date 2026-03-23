#include "ntp_service/ntp_service.hpp"

namespace ntp_service {

NtpService& NtpService::instance() {
    static NtpService service;
    return service;
}

common::Result<void> NtpService::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<void> NtpService::sync() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    return common::Result<void>();
}

}  // namespace ntp_service
