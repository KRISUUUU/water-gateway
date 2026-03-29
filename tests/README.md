# Tests

## Overview

The test suite consists of host-runnable unit tests that verify pure logic
without requiring ESP-IDF or target hardware.

## Running Host Tests

```bash
cd tests/host
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

Requirements: CMake >= 3.16, C++17 compiler (g++ or clang++).

## Test Files

| Test | Module Tested | Coverage |
|------|--------------|----------|
| `test_config_validation.cpp` | `config_store/config_validation` | All validation rules, boundary values, hostname chars |
| `test_config_migration.cpp` | `config_store/config_migration` | v0→v1 migration, future version rejection, field preservation |
| `test_dedup.cpp` | `dedup_service` | Seen/not-seen, window expiry, prune, entry counting |
| `test_mqtt_payloads.cpp` | `mqtt_service/mqtt_topics` + `mqtt_payloads` | Topic generation, JSON payload structure, field presence |
| `test_auth_helpers.cpp` | `auth_service` | Password hashing, verification, edge cases |
| `test_auth_login_policy.cpp` | `auth_service` + `config_store` | Provisioning-mode bootstrap login policy and rate-limit behavior |
| `test_health_logic.cpp` | `health_monitor` | State transitions, counter increments, null rejection |
| `test_wmbus_pipeline.cpp` | `wmbus_minimal_pipeline` | Hex encoding/decoding, frame conversion, field accessors |
| `test_config_store_status.cpp` | `config_store` | Runtime status counters and fallback/save visibility |
| `test_meter_registry.cpp` | `meter_registry` | Meter observation, watchlist CRUD/filtering logic |
| `test_ota_manager.cpp` | `ota_manager` | OTA lifecycle guard paths and state transitions in host mode |
| `test_metrics_service.cpp` | `metrics_service` | Queue/task metrics snapshots and reset behavior |
| `test_diagnostics_service.cpp` | `diagnostics_service` | Diagnostics snapshot/JSON fields for observability |
| `test_radio_state_machine.cpp` | `radio_state_machine` | Soft-failure escalation and recovery behavior |
| `test_security_posture.cpp` | `common/security_posture` | Compile-time build hardening posture derivation and production-ready predicate |

## Fixtures

`fixtures/sample_frames.json` contains sample WMBus frame data for replay-style tests.
See `fixtures/README.md` for format documentation.

## Host Test Architecture

Tests compile component `.cpp` files directly and use `host_test_stubs.hpp` to
stub ESP-IDF macros (`ESP_LOG*`, `esp_err_t`, etc.). The `HOST_TEST_BUILD`
preprocessor define controls conditional compilation in component sources.

## What Is NOT Tested (and Why)

See `docs/TESTING.md` for the full list of untestable areas and manual test plans.
