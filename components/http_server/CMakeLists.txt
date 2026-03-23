idf_component_register(
    SRCS "src/http_server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common auth_service esp_http_server
)
