idf_component_register(
    SRCS "src/api_handlers.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common http_server auth_service
)
