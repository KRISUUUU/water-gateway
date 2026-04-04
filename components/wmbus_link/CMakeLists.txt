idf_component_register(
    SRCS
        "src/wmbus_link.cpp"
        "src/wmbus_tmode_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common wmbus_tmode_rx rf_diagnostics protocol_driver
)
