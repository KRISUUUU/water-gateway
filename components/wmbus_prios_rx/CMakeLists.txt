idf_component_register(
    SRCS
        "src/prios_capture_driver.cpp"
        "src/prios_capture_service.cpp"
        "src/prios_bringup_session.cpp"
        "src/prios_analyzer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        common
        protocol_driver
        radio_cc1101
        wmbus_tmode_rx
)
