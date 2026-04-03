idf_component_register(
    SRCS
        "src/packet_length_strategy.cpp"
        "src/rx_session_engine.cpp"
        "src/wmbus_block_validation.cpp"
        "src/wmbus_tmode_framer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common radio_cc1101 rf_diagnostics
)
