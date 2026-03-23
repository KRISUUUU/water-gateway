idf_component_register(
    SRCS "src/diagnostics_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common health_monitor radio_cc1101 mqtt_service wifi_manager
)
