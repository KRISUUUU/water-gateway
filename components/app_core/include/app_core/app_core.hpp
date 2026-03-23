#pragma once

#include "common/result.hpp"
#include "common/types.hpp"

namespace app_core {

class AppCore {
public:
    // Main entry point. Called from app_main().
    // Initializes all services, determines startup mode, creates tasks.
    void start();

private:
    // Phase 1: Initialize foundation services (event bus, config, storage)
    common::Result<void> initialize_foundations();

    // Phase 2: Determine startup mode (normal vs provisioning)
    common::SystemMode determine_start_mode();

    // Phase 3a: Start provisioning mode (AP + config form)
    common::Result<void> start_provisioning();

    // Phase 3b: Start normal runtime (WiFi STA, MQTT, radio, HTTP, etc.)
    common::Result<void> start_normal_runtime();

    // Create FreeRTOS tasks for normal operation
    void create_runtime_tasks();
};

} // namespace app_core
