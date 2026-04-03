idf_component_register(
    SRCS "src/diagnostics_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common radio_cc1101 radio_state_machine mqtt_service wifi_manager ntp_service
             metrics_service health_monitor rf_diagnostics json
)
