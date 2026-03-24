idf_component_register(
    SRCS "src/wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common config_store event_bus esp_wifi esp_event esp_netif
)
