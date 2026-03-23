#include "watchdog_service/watchdog_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace watchdog_service {

WatchdogService& WatchdogService::instance() {
    static WatchdogService service;
    return service;
}

common::Result<void> WatchdogService::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    esp_task_wdt_config_t cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    twdt_configured_ = true;
#endif

    initialized_ = true;
    return common::Result<void>::ok();
}

#ifndef HOST_TEST_BUILD
common::Result<void> WatchdogService::register_task(TaskHandle_t task_handle) {
#else
common::Result<void> WatchdogService::register_task(void* task_handle) {
#endif
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    if (!twdt_configured_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    TaskHandle_t h =
        task_handle ? static_cast<TaskHandle_t>(task_handle) : xTaskGetCurrentTaskHandle();
    esp_err_t err = esp_task_wdt_add(h);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
#else
    (void)task_handle;
#endif

    return common::Result<void>::ok();
}

common::Result<void> WatchdogService::feed() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
#endif

    return common::Result<void>::ok();
}

} // namespace watchdog_service
