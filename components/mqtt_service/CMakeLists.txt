idf_component_register(
    SRCS
        "src/mqtt_service.cpp"
        "src/mqtt_topics.cpp"
        "src/mqtt_payloads.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common config_store event_bus mqtt json
)
