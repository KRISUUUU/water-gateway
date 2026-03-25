# Release Readiness Checklist

## Scope

This checklist is for the final pre-release phase after software hardening is complete.
It separates what can be validated now from what still requires real hardware.

## Pre-Hardware Checklist

- [ ] Host tests pass (`ctest --output-on-failure` in `tests/host/build-gcc`)
- [ ] ESP-IDF build passes (`idf.py build`)
- [ ] Static web assets generated and flashed (`build/storage.bin`)
- [ ] OTA endpoints coherent:
  - [ ] `POST /api/ota/upload` (binary upload)
  - [ ] `POST /api/ota/url` (HTTPS URL OTA)
  - [ ] `GET /api/ota/status` (state/progress/message)
- [ ] Auth endpoints coherent:
  - [ ] `POST /api/auth/login`
  - [ ] `POST /api/auth/logout`
  - [ ] `POST /api/auth/password`
- [ ] Docs aligned with current behavior

## Post-Hardware Bring-Up Checklist

Run `docs/HARDWARE_BRINGUP.md` end-to-end, then verify:

- [ ] Live telegrams page receives real RF traffic
- [ ] Detected meters and watchlist map correctly to observed traffic
- [ ] MQTT `rf/raw` payload contains `raw_hex`, `meter_key`, and RF metadata
- [ ] Diagnostics counters are coherent with on-air activity

## 24-48h Soak Checklist

Collect and review trends every 1-2h:

- [ ] `fifo_overflows`
- [ ] `radio_resets`
- [ ] `mqtt_failures`
- [ ] `free_heap_bytes` and `min_free_heap_bytes`
- [ ] watchdog/reset stability
- [ ] no crash loops / unexpected reboots

## OTA Rollback Validation Checklist

- [ ] Upload known-good image and verify successful boot
- [ ] Trigger controlled fail-before-mark-valid scenario
- [ ] Verify bootloader rollback to previous slot
- [ ] Confirm OTA state and logs reflect failure path honestly
- [ ] Confirm support bundle captures OTA state and failure context

## Release Candidate Checklist

- [ ] Docs freeze (README + operations/security/ota/testing/troubleshooting)
- [ ] Version bump and changelog/release notes
- [ ] Final binary size check against OTA partition margin
- [ ] CI green on main workflows (host tests, format check, ESP-IDF build)
- [ ] Tag release candidate after all above checks pass
