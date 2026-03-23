idf_component_register(
    SRCS
        "src/ota_manager.cpp"
        "src/ota_state.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common event_bus
)
