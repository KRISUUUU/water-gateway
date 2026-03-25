idf_component_register(
    SRCS "src/api_handlers.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        common
        http_server
        auth_service
        config_store
        mqtt_service
        diagnostics_service
        metrics_service
        health_monitor
        ota_manager
        provisioning_manager
        radio_state_machine
        meter_registry
        persistent_log_buffer
        support_bundle_service
        json
)
