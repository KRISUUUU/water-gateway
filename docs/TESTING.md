# Testing Strategy

## Overview

The testing strategy separates **host-testable pure logic** from
**hardware-dependent code** that requires an ESP32 target.

Host tests run on the development machine using CMake + CTest. They use a native
C++ toolchain (GCC, Clang, or MSVC). cJSON may be vendored via FetchContent if
ESP-IDF’s `cJSON` path is not found.

## Host Test Architecture

### Build System

```
tests/host/
├── CMakeLists.txt
├── host_test_stubs.hpp
├── test_config_validation.cpp
├── test_config_migration.cpp
├── test_dedup.cpp
├── test_mqtt_payloads.cpp
├── test_auth_helpers.cpp
├── test_health_logic.cpp
├── test_wmbus_pipeline.cpp
├── test_meter_registry.cpp
├── test_ota_manager.cpp
```

The CMakeLists:

1. Adds component include directories
2. Compiles the listed `.cpp` sources for each test target
3. Defines `HOST_TEST_BUILD` globally
4. Registers one CTest per executable

### Running Host Tests

```bash
cd tests/host
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

Nine tests are registered (names match the `test_*` executables).

## Test Coverage Map

| Test File | Module Under Test | What Is Tested |
|-----------|------------------|----------------|
| `test_config_validation.cpp` | `config_store/config_validation` | Invalid/missing fields, hostname, MQTT host/port, radio frequency band |
| `test_config_migration.cpp` | `config_store/config_migration` | v0→v1 migration; unknown future version rejected |
| `test_dedup.cpp` | `dedup_service` | `seen_recently` / `remember` / window / `prune` |
| `test_mqtt_payloads.cpp` | `mqtt_topics`, `mqtt_payloads` | Topic paths, JSON fields for status/telemetry/raw |
| `test_auth_helpers.cpp` | `auth_service` | Salted SHA-256 format, verify_password |
| `test_health_logic.cpp` | `health_monitor` | report_healthy / warning / error and snapshot |
| `test_wmbus_pipeline.cpp` | `wmbus_minimal_pipeline` | 3-of-6 encode/decode round-trip fixtures, EN 13757 DLL CRC acceptance, invalid symbol/CRC rejection, `l_field` / identity helpers, hex helpers |
| `test_meter_registry.cpp` | `meter_registry` | observe_frame, watchlist, telegram filters |
| `test_ota_manager.cpp` | `ota_manager` | Host-mode upload lifecycle and status |

### Fixture Data

`tests/fixtures/sample_frames.json` exists for reference. `test_wmbus_pipeline.cpp` builds frames inline (including 3-of-6–encoded radio buffers) so tests do not depend on SPIFFS or IDF.

## What Cannot Be Host-Tested

| Area | Reason | Manual / HIL approach |
|------|--------|------------------------|
| CC1101 SPI and FIFO | Hardware | Logic analyzer; meter in range |
| WiFi / lwIP | IDF stack | On-device |
| NVS | IDF | On-device power-cycle |
| OTA partitions | IDF | Flash and reboot |
| HTTP + SPIFFS | IDF | `curl` + browser |
| End-to-end RF → pipeline | Air + PHY | See `docs/HARDWARE_BRINGUP.md` |

## Static Analysis

- `.clang-format` — project style
- CI may run `clang-format --dry-run --Werror` on `components/` and `main/`

## CI Pipeline (typical)

1. Host: `cmake` + `ctest` in `tests/host/`
2. Format: clang-format check
3. Firmware: `idf.py build` (ESP-IDF environment)

## Test Gaps (Tracked)

| Gap | Notes |
|-----|--------|
| Fuzzing | Not implemented |
| Load / soak | Requires hardware |
| TLS broker | Manual |
