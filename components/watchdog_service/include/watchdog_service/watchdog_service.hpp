#pragma once

#include "common/result.hpp"

#ifndef HOST_TEST_BUILD
#include "freertos/FreeRTOS.h"
#endif

namespace watchdog_service {

class WatchdogService {
  public:
    static WatchdogService& instance();

    common::Result<void> initialize();

#ifndef HOST_TEST_BUILD
    common::Result<void> register_task(TaskHandle_t task_handle = nullptr);
#else
    common::Result<void> register_task(void* task_handle = nullptr);
#endif

    common::Result<void> feed();

  private:
    WatchdogService() = default;

#ifndef HOST_TEST_BUILD
    bool twdt_configured_{false};
#endif
    bool initialized_{false};
};

} // namespace watchdog_service
