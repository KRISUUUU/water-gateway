idf_component_register(
    SRCS
        "src/cc1101_device.cpp"
        "src/cc1101_hal.cpp"
        "src/cc1101_irq.cpp"
        "src/radio_cc1101.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common driver
)
