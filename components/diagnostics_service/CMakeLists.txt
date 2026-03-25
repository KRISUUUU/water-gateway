idf_component_register(
    SRCS "src/diagnostics_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common radio_cc1101 mqtt_service wifi_manager metrics_service health_monitor json
)
