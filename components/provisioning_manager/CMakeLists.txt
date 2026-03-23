idf_component_register(
    SRCS "src/provisioning_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common event_bus config_store wifi_manager
)
