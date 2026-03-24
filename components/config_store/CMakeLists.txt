idf_component_register(
    SRCS
        "src/config_store.cpp"
        "src/config_validation.cpp"
        "src/config_migration.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common storage_service event_bus nvs_flash
)
