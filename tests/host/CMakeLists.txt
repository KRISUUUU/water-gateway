cmake_minimum_required(VERSION 3.16)
project(water_gateway_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# Root of the project
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Component include directories
set(COMP_DIR "${PROJECT_ROOT}/components")

# Common include paths needed by most tests
set(COMMON_INCLUDES
    "${COMP_DIR}/common/include"
    "${COMP_DIR}/event_bus/include"
    "${COMP_DIR}/config_store/include"
    "${COMP_DIR}/storage_service/include"
    "${COMP_DIR}/radio_cc1101/include"
    "${COMP_DIR}/wmbus_minimal_pipeline/include"
    "${COMP_DIR}/dedup_service/include"
    "${COMP_DIR}/telegram_router/include"
    "${COMP_DIR}/meter_registry/include"
    "${COMP_DIR}/mqtt_service/include"
    "${COMP_DIR}/auth_service/include"
    "${COMP_DIR}/ota_manager/include"
    "${COMP_DIR}/health_monitor/include"
    "${COMP_DIR}/metrics_service/include"
    "${COMP_DIR}/persistent_log_buffer/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Compile definitions for host test builds
add_compile_definitions(HOST_TEST_BUILD)

# ---- Helper function to add a host test ----
function(add_host_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${ARGN})
    target_include_directories(${TEST_NAME} PRIVATE ${COMMON_INCLUDES})
    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ---- test_config_validation ----
add_host_test(test_config_validation
    test_config_validation.cpp
    "${COMP_DIR}/config_store/src/config_validation.cpp"
)

# ---- test_config_migration ----
add_host_test(test_config_migration
    test_config_migration.cpp
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_dedup ----
add_host_test(test_dedup
    test_dedup.cpp
    "${COMP_DIR}/dedup_service/src/dedup_service.cpp"
)

# ---- test_mqtt_payloads ----
add_host_test(test_mqtt_payloads
    test_mqtt_payloads.cpp
    "${COMP_DIR}/mqtt_service/src/mqtt_topics.cpp"
    "${COMP_DIR}/mqtt_service/src/mqtt_payloads.cpp"
)

# ---- test_auth_helpers ----
add_host_test(test_auth_helpers
    test_auth_helpers.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/auth_service/src/auth_service.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_health_logic ----
add_host_test(test_health_logic
    test_health_logic.cpp
    "${COMP_DIR}/health_monitor/src/health_monitor.cpp"
)

# ---- test_wmbus_pipeline ----
add_host_test(test_wmbus_pipeline
    test_wmbus_pipeline.cpp
    "${COMP_DIR}/wmbus_minimal_pipeline/src/wmbus_pipeline.cpp"
)

# ---- test_meter_registry ----
add_host_test(test_meter_registry
    test_meter_registry.cpp
    "${COMP_DIR}/meter_registry/src/meter_registry.cpp"
    "${COMP_DIR}/storage_service/src/storage_service.cpp"
    "${COMP_DIR}/wmbus_minimal_pipeline/src/wmbus_pipeline.cpp"
)

# ---- test_ota_manager ----
add_host_test(test_ota_manager
    test_ota_manager.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/ota_manager/src/ota_manager.cpp"
)
