idf_component_register(
    SRCS "src/dedup_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common wmbus_minimal_pipeline
)
