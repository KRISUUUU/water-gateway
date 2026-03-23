# Testing Strategy

## Overview

The testing strategy separates **host-testable pure logic** from
**hardware-dependent code** that requires an ESP32 target.

Host tests run on the development machine using CMake + CTest, without any
ESP-IDF toolchain dependency. This enables fast iteration and CI integration.

## Host Test Architecture

### Build System

```
tests/host/
├── CMakeLists.txt          → Top-level test build
├── test_config_validation.cpp
├── test_config_migration.cpp
├── test_dedup.cpp
├── test_mqtt_payloads.cpp
├── test_auth_helpers.cpp
├── test_health_logic.cpp
└── test_wmbus_pipeline.cpp
```

Host tests include component source files directly and provide minimal stubs
for ESP-IDF APIs that the logic code references. The test CMakeLists.txt:

1. Defines include paths pointing to component headers
2. Compiles the subset of `.cpp` files that contain pure logic
3. Provides a `host_stubs.h` with minimal type/macro definitions (e.g., `ESP_LOG*` as no-ops)
4. Creates one executable per test file
5. Registers each with CTest

### Running Host Tests

```bash
cd tests/host
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

## Test Coverage Map

### Unit Tests

| Test File | Module Under Test | What Is Tested |
|-----------|------------------|----------------|
| `test_config_validation.cpp` | `config_store/config_validation` | Valid configs pass; missing SSID, empty hostname, invalid port, out-of-range frequency all produce correct `ValidationIssue` entries |
| `test_config_migration.cpp` | `config_store/config_migration` | Migration from v0 (unversioned) to current; migration of unknown future version returns error; field defaults are applied correctly |
| `test_dedup.cpp` | `dedup_service` | `seen_recently` returns false for new key; returns true after `remember`; returns false after window expires; `prune` removes expired entries |
| `test_mqtt_payloads.cpp` | `mqtt_service/mqtt_payloads` | Status payload contains `"online"` field; raw frame payload contains `"hex"`, `"rssi"`, `"timestamp"` fields; topic builder produces correct path structure |
| `test_auth_helpers.cpp` | `auth_service` (pure logic subset) | Password hash generation and verification; token format validation; session expiry logic |
| `test_health_logic.cpp` | `health_monitor` | State transitions: starting→healthy, healthy→warning, warning→error; counter increments on warnings/errors; recovery back to healthy |
| `test_wmbus_pipeline.cpp` | `wmbus_minimal_pipeline` | `from_radio_frame` produces correct hex encoding; metadata fields (RSSI, LQI, CRC, length) are preserved; `bytes_to_hex` handles empty and max-length inputs |

### Fixture-Based Replay Tests

The `tests/fixtures/sample_frames.json` file contains captured or synthetic
WMBus frame data. The `test_wmbus_pipeline.cpp` test loads these fixtures and
verifies that the pipeline produces expected outputs.

Fixture format:
```json
[
  {
    "name": "basic_t_mode_frame",
    "raw_bytes_hex": "2C44...",
    "expected_length": 44,
    "expected_crc_ok": true,
    "rssi_dbm": -65,
    "lqi": 45
  }
]
```

### Integration-Like Tests (Host)

These test multi-module interactions without hardware:

| Scenario | Modules Involved | What Is Tested |
|----------|-----------------|----------------|
| Frame → route → payload | `wmbus_pipeline`, `telegram_router`, `dedup_service`, `mqtt_payloads` | A raw frame is converted, routed (not duplicate), and produces a valid MQTT JSON payload |
| Config import round-trip | `config_store`, `config_validation` | A config is serialized, imported, validated, and the result matches the original (except redacted fields) |
| Dedup window boundary | `dedup_service`, `telegram_router` | Same frame sent twice within window is suppressed; sent after window expires is published |

## What Cannot Be Host-Tested

| Area | Reason | Manual/HIL Test Approach |
|------|--------|--------------------------|
| CC1101 SPI communication | Requires SPI peripheral and CC1101 hardware | Logic analyzer on SPI bus; verify register writes match expected sequence |
| WiFi connection lifecycle | Requires ESP-IDF WiFi stack | Flash to device; verify connection to AP; verify reconnect after AP restart |
| NVS read/write | Requires ESP-IDF NVS partition | Flash to device; save config; reboot; verify config survives |
| HTTP server and routing | Requires ESP-IDF httpd | Flash to device; run `curl` test script against endpoints |
| SPIFFS file serving | Requires ESP-IDF VFS + SPIFFS | Flash with web assets; verify pages load in browser |
| OTA partition operations | Requires OTA partition layout | Flash to device; upload firmware via web UI; verify new version boots; verify rollback |
| mDNS advertisement | Requires lwIP mDNS | Flash to device; verify `hostname.local` resolves |
| SNTP time sync | Requires network and NTP server | Flash to device; verify system time is correct after sync |
| Watchdog behavior | Requires hardware watchdog timer | Flash to device; intentionally hang a task; verify watchdog triggers reset |

## Static Analysis

### Formatting
- `.clang-format` enforces LLVM-based style with 4-space indent, 100-column limit
- CI check: `find components main -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror`

### Future: clang-tidy
- ESP-IDF-compatible checks for common C++ issues
- Checks: `modernize-*`, `bugprone-*`, `readability-*`
- Suppressions for ESP-IDF macro patterns (`ESP_LOG*`, `ESP_ERROR_CHECK`)

## CI Pipeline

```yaml
jobs:
  host-tests:
    # Runs on any Linux/macOS/Windows runner
    # No ESP-IDF toolchain needed
    steps:
      - cmake + build + ctest in tests/host/

  format-check:
    steps:
      - clang-format --dry-run --Werror on all source files

  esp-idf-build:
    # Requires ESP-IDF Docker image (espressif/idf:v5.x)
    steps:
      - idf.py build
```

## Test Gaps (Tracked)

| Gap | Reason | Plan |
|-----|--------|------|
| No fuzz testing | Scope limitation | Future: fuzz MQTT payload builders and config import |
| No load testing | Requires hardware | Future: frame generator + device under test |
| No multi-device testing | Requires multiple devices | Future: test MQTT topic isolation |
| No TLS certificate testing | Requires broker setup | Manual test with TLS-enabled broker |
| No power-loss testing | Requires hardware setup | Future: test NVS integrity after unexpected power loss |
