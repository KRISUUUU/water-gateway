idf_component_register(
    SRCS "src/app_core.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common event_bus config_store
)
