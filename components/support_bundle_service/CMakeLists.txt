idf_component_register(
    SRCS "src/support_bundle_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common diagnostics_service metrics_service health_monitor config_store persistent_log_buffer meter_registry ota_manager json
)
