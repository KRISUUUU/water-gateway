# Release Readiness Checklist

## Scope

Pre-release verification after software changes; hardware-specific items are separate.

## Pre-Hardware Checklist

- [ ] Host tests pass: `cd tests/host && mkdir -p build && cd build && cmake .. && cmake --build . && ctest --output-on-failure`
- [ ] ESP-IDF build passes: `idf.py build`
- [ ] Web assets included in SPIFFS image (`storage` partition flashed)
- [ ] OTA endpoints exercised in a safe environment:
  - [ ] `POST /api/ota/upload` (binary body)
  - [ ] `POST /api/ota/url` (HTTPS URL)
  - [ ] `GET /api/ota/status`
- [ ] Auth endpoints: login, logout, password change
- [ ] Documentation matches current code (`README.md`, `docs/*.md`)

## Post-Hardware Bring-Up

Follow `docs/HARDWARE_BRINGUP.md`, then:

- [ ] Live Telegrams shows frames when meters transmit (or test transmitter)
- [ ] Detected meters / watchlist behave as expected
- [ ] MQTT `rf/raw` payloads include `raw_hex`, metadata, and `meter_key`
- [ ] Telemetry counters move consistently with RF activity

## Soak (24–48h)

Sample periodically:

- [ ] `fifo_overflows`, `frames_incomplete`, `frames_dropped_too_long`
- [ ] `mqtt_publish_failures`, radio recoveries
- [ ] `free_heap_bytes`, `min_free_heap_bytes`
- [ ] Unexpected reboots or watchdog triggers

## OTA Rollback

- [ ] Flash known-good build via OTA
- [ ] Induce failed boot path if testing rollback (controlled scenario)
- [ ] Confirm previous slot boots and OTA state in UI/diagnostics is coherent

## Release Candidate

- [ ] Docs and version strings ready
- [ ] Binary size within OTA partition margin
- [ ] CI green (host tests + format + firmware build)
- [ ] Tag / release notes
