idf_component_register(
    SRCS "src/metrics_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common esp_timer heap
)
