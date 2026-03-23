#pragma once

#include "common/result.hpp"

namespace watchdog_service {

class WatchdogService {
public:
    static WatchdogService& instance();

    common::Result<void> initialize();
    common::Result<void> register_current_task();
    common::Result<void> feed();

private:
    WatchdogService() = default;
    bool initialized_{false};
};

}  // namespace watchdog_service
