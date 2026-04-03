idf_component_register(
    SRCS "src/meter_registry.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common storage_service wmbus_link
)
