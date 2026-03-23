#include "watchdog_service/watchdog_service.hpp"

namespace watchdog_service {

WatchdogService& WatchdogService::instance() {
    static WatchdogService service;
    return service;
}

common::Result<void> WatchdogService::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<void> WatchdogService::register_current_task() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    return common::Result<void>();
}

common::Result<void> WatchdogService::feed() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    return common::Result<void>();
}

}  // namespace watchdog_service
