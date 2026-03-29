cmake_minimum_required(VERSION 3.16)
project(water_gateway_host_tests C CXX)

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
    "${COMP_DIR}/radio_state_machine/include"
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

set(CJSON_DIR "")
if(DEFINED ENV{IDF_PATH})
    set(CJSON_DIR "$ENV{IDF_PATH}/components/json/cJSON")
endif()
if(CJSON_DIR STREQUAL "" OR NOT EXISTS "${CJSON_DIR}/cJSON.c")
    if(EXISTS "${PROJECT_ROOT}/../Espressif/v5.2/esp-idf/components/json/cJSON/cJSON.c")
        set(CJSON_DIR "${PROJECT_ROOT}/../Espressif/v5.2/esp-idf/components/json/cJSON")
    elseif(EXISTS "E:/Espressif/v5.2/esp-idf/components/json/cJSON/cJSON.c")
        set(CJSON_DIR "E:/Espressif/v5.2/esp-idf/components/json/cJSON")
    endif()
endif()

include(FetchContent)

set(CJSON_SRC "")
if(EXISTS "${CJSON_DIR}/cJSON.c")
    list(APPEND COMMON_INCLUDES "${CJSON_DIR}")
    set(CJSON_SRC "${CJSON_DIR}/cJSON.c")
else()
    message(STATUS "ESP-IDF cJSON not found. Fetching from GitHub for Host Tests...")
    FetchContent_Declare(
        cJSON_ext
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.17
    )
    FetchContent_Populate(cJSON_ext)
    list(APPEND COMMON_INCLUDES "${cjson_ext_SOURCE_DIR}")
    set(CJSON_SRC "${cjson_ext_SOURCE_DIR}/cJSON.c")
endif()

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
    ${CJSON_SRC}
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

# ---- test_metrics_service ----
add_host_test(test_metrics_service
    test_metrics_service.cpp
    "${COMP_DIR}/metrics_service/src/metrics_service.cpp"
)

# ---- test_radio_state_machine ----
add_host_test(test_radio_state_machine
    test_radio_state_machine.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/radio_cc1101/src/radio_cc1101.cpp"
    "${COMP_DIR}/radio_state_machine/src/radio_state_machine.cpp"
)
