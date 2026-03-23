idf_component_register(
    SRCS "src/support_bundle_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common diagnostics_service persistent_log_buffer
)
