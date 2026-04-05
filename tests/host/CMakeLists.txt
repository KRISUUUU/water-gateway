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
    "${COMP_DIR}/protocol_driver/include"
    "${COMP_DIR}/event_bus/include"
    "${COMP_DIR}/config_store/include"
    "${COMP_DIR}/storage_service/include"
    "${COMP_DIR}/radio_cc1101/include"
    "${COMP_DIR}/radio_state_machine/include"
    "${COMP_DIR}/rf_diagnostics/include"
    "${COMP_DIR}/wmbus_tmode_rx/include"
    "${COMP_DIR}/wmbus_link/include"
    "${COMP_DIR}/wmbus_prios_rx/include"
    "${COMP_DIR}/dedup_service/include"
    "${COMP_DIR}/telegram_router/include"
    "${COMP_DIR}/meter_registry/include"
    "${COMP_DIR}/mqtt_service/include"
    "${COMP_DIR}/wifi_manager/include"
    "${COMP_DIR}/ntp_service/include"
    "${COMP_DIR}/diagnostics_service/include"
    "${COMP_DIR}/auth_service/include"
    "${COMP_DIR}/api_handlers/include"
    "${COMP_DIR}/ota_manager/include"
    "${COMP_DIR}/health_monitor/include"
    "${COMP_DIR}/metrics_service/include"
    "${COMP_DIR}/persistent_log_buffer/include"
    "${COMP_DIR}/app_core/include"
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

# ---- test_config_store_status ----
add_host_test(test_config_store_status
    test_config_store_status.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_api_config_json ----
add_host_test(test_api_config_json
    test_api_config_json.cpp
    "${COMP_DIR}/api_handlers/src/config_json_codec.cpp"
    "${COMP_DIR}/auth_service/src/auth_service.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    ${CJSON_SRC}
)

# ---- test_dedup ----
add_host_test(test_dedup
    test_dedup.cpp
    "${COMP_DIR}/dedup_service/src/dedup_service.cpp"
)

# ---- test_mqtt_payloads ----
add_host_test(test_mqtt_payloads
    test_mqtt_payloads.cpp
    "${COMP_DIR}/mqtt_service/src/mqtt_publish.cpp"
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

# ---- test_auth_pbkdf2 ----
add_host_test(test_auth_pbkdf2
    test_auth_pbkdf2.cpp
    "${COMP_DIR}/auth_service/src/auth_service.cpp"
    "${COMP_DIR}/persistent_log_buffer/src/persistent_log_buffer.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
)

# ---- test_auth_login_policy ----
add_host_test(test_auth_login_policy
    test_auth_login_policy.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/auth_service/src/auth_service.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_auth_rate_limit ----
add_host_test(test_auth_rate_limit
    test_auth_rate_limit.cpp
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

# ---- test_radio_cc1101 ----
add_host_test(test_radio_cc1101
    test_radio_cc1101.cpp
    "${COMP_DIR}/radio_cc1101/src/cc1101_device.cpp"
    "${COMP_DIR}/radio_cc1101/src/cc1101_hal.cpp"
    "${COMP_DIR}/radio_cc1101/src/cc1101_irq.cpp"
    "${COMP_DIR}/radio_cc1101/src/radio_cc1101.cpp"
)

# ---- test_meter_registry ----
add_host_test(test_meter_registry
    test_meter_registry.cpp
    "${COMP_DIR}/meter_registry/src/meter_registry.cpp"
    "${COMP_DIR}/storage_service/src/storage_service.cpp"
    "${COMP_DIR}/wmbus_link/src/wmbus_link.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
)

# ---- test_downstream_exact_flow ----
add_host_test(test_downstream_exact_flow
    test_downstream_exact_flow.cpp
    "${COMP_DIR}/mqtt_service/src/mqtt_publish.cpp"
    "${COMP_DIR}/telegram_router/src/telegram_router.cpp"
    "${COMP_DIR}/dedup_service/src/dedup_service.cpp"
    "${COMP_DIR}/wmbus_link/src/wmbus_link.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_tmode_framer.cpp"
)

# ---- test_ota_manager ----
add_host_test(test_ota_manager
    test_ota_manager.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/ota_manager/src/ota_manager.cpp"
)

# ---- test_event_bus ----
add_host_test(test_event_bus
    test_event_bus.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
)

# ---- test_metrics_service ----
add_host_test(test_metrics_service
    test_metrics_service.cpp
    "${COMP_DIR}/metrics_service/src/metrics_service.cpp"
)

# ---- test_wifi_manager ----
add_host_test(test_wifi_manager
    test_wifi_manager.cpp
    "${COMP_DIR}/wifi_manager/src/wifi_manager.cpp"
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_security_posture ----
add_host_test(test_security_posture
    test_security_posture.cpp
)

# ---- test_diagnostics_service ----
add_host_test(test_diagnostics_service
    test_diagnostics_service.cpp
    "${COMP_DIR}/rf_diagnostics/src/rf_diagnostics.cpp"
    "${COMP_DIR}/diagnostics_service/src/diagnostics_service.cpp"
    "${COMP_DIR}/metrics_service/src/metrics_service.cpp"
    "${COMP_DIR}/health_monitor/src/health_monitor.cpp"
    "${COMP_DIR}/mqtt_service/src/mqtt_publish.cpp"
    "${COMP_DIR}/mqtt_service/src/mqtt_service.cpp"
    "${COMP_DIR}/wifi_manager/src/wifi_manager.cpp"
    "${COMP_DIR}/config_store/src/config_store.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
    "${COMP_DIR}/radio_cc1101/src/cc1101_device.cpp"
    "${COMP_DIR}/radio_cc1101/src/cc1101_hal.cpp"
    "${COMP_DIR}/radio_cc1101/src/cc1101_irq.cpp"
    "${COMP_DIR}/radio_cc1101/src/radio_cc1101.cpp"
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/radio_state_machine/src/radio_state_machine.cpp"
    "${COMP_DIR}/ntp_service/src/ntp_service.cpp"
    ${CJSON_SRC}
)

# ---- test_rf_diagnostics ----
add_host_test(test_rf_diagnostics
    test_rf_diagnostics.cpp
    "${COMP_DIR}/rf_diagnostics/src/rf_diagnostics.cpp"
    ${CJSON_SRC}
)

# ---- test_radio_state_machine ----
add_host_test(test_radio_state_machine
    test_radio_state_machine.cpp
    "${COMP_DIR}/event_bus/src/event_bus.cpp"
    "${COMP_DIR}/radio_cc1101/src/radio_cc1101.cpp"
    "${COMP_DIR}/radio_state_machine/src/radio_state_machine.cpp"
)

# ---- test_radio_rx_wake_model ----
add_host_test(test_radio_rx_wake_model
    test_radio_rx_wake_model.cpp
)

# ---- test_wmbus_tmode_framer ----
add_host_test(test_wmbus_tmode_framer
    test_wmbus_tmode_framer.cpp
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_tmode_framer.cpp"
)

# ---- test_rx_session_engine ----
add_host_test(test_rx_session_engine
    test_rx_session_engine.cpp
    "${COMP_DIR}/wmbus_tmode_rx/src/packet_length_strategy.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/rx_session_engine.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_tmode_framer.cpp"
)

# ---- test_protocol_driver ----
add_host_test(test_protocol_driver
    test_protocol_driver.cpp
    "${COMP_DIR}/protocol_driver/src/radio_protocol_scheduler.cpp"
)

# ---- test_radio_scheduler_config ----
add_host_test(test_radio_scheduler_config
    test_radio_scheduler_config.cpp
    "${COMP_DIR}/protocol_driver/src/radio_protocol_scheduler.cpp"
    "${COMP_DIR}/protocol_driver/src/radio_profile_manager.cpp"
    "${COMP_DIR}/config_store/src/config_validation.cpp"
    "${COMP_DIR}/config_store/src/config_migration.cpp"
)

# ---- test_prios_capture ----
add_host_test(test_prios_capture
    test_prios_capture.cpp
    "${COMP_DIR}/wmbus_prios_rx/src/prios_capture_driver.cpp"
    "${COMP_DIR}/wmbus_prios_rx/src/prios_capture_service.cpp"
)

# ---- test_prios_fixture_analyzer ----
add_host_test(test_prios_fixture_analyzer
    test_prios_fixture_analyzer.cpp
    "${COMP_DIR}/wmbus_prios_rx/src/prios_analyzer.cpp"
)

# ---- test_prios_export ----
add_host_test(test_prios_export
    test_prios_export.cpp
    "${COMP_DIR}/wmbus_prios_rx/src/prios_export.cpp"
    ${CJSON_SRC}
)

# ---- test_prios_framer_readiness ----
# Documents which protocol facts are still missing before a framer can be
# written.  Capture-mode smoke tests must always pass; framer-readiness
# status is printed but does NOT fail the suite (the blocked state is
# expected until hardware captures arrive).
add_host_test(test_prios_framer_readiness
    test_prios_framer_readiness.cpp
    "${COMP_DIR}/wmbus_prios_rx/src/prios_capture_driver.cpp"
    "${COMP_DIR}/wmbus_prios_rx/src/prios_capture_service.cpp"
)

# ---- test_wmbus_tmode_driver ----
add_host_test(test_wmbus_tmode_driver
    test_wmbus_tmode_driver.cpp
    "${COMP_DIR}/wmbus_link/src/wmbus_link.cpp"
    "${COMP_DIR}/wmbus_link/src/wmbus_tmode_driver.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_tmode_framer.cpp"
    "${COMP_DIR}/rf_diagnostics/src/rf_diagnostics.cpp"
    ${CJSON_SRC}
)

# ---- test_wmbus_link ----
add_host_test(test_wmbus_link
    test_wmbus_link.cpp
    "${COMP_DIR}/wmbus_link/src/wmbus_link.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_block_validation.cpp"
    "${COMP_DIR}/wmbus_tmode_rx/src/wmbus_tmode_framer.cpp"
    "${COMP_DIR}/rf_diagnostics/src/rf_diagnostics.cpp"
    ${CJSON_SRC}
)
