idf_component_register(
    SRCS "src/wmbus_pipeline.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common radio_cc1101 wmbus_tmode_rx wmbus_link
)
