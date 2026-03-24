# OTA (Over-The-Air) Updates

## Overview

The firmware currently exposes two OTA API paths:
1. **Local upload endpoint** — `POST /api/ota/upload` exists but is intentionally not implemented yet (returns HTTP 501)
2. **URL OTA** — `POST /api/ota/url` starts OTA from an HTTPS URL

Both methods use ESP-IDF's OTA APIs with rollback support.

## Partition Strategy

| Partition | Size | Purpose |
|-----------|------|---------|
| `ota_0` | 1.5 MB | Active app slot (also used for initial serial flash) |
| `ota_1` | 1.5 MB | Inactive OTA update slot |
| `otadata` | 8 KB | Tracks active boot partition |
| `storage` | 896 KB | SPIFFS web assets / local storage |

OTA writes to the **inactive** OTA partition. After commit, the device reboots
into the newly written partition.

## Update Flow

### Local Upload (not implemented yet)

The upload endpoint is present for API contract stability, but currently returns:

```json
{"error":"not_implemented","detail":"multipart firmware upload not yet implemented"}
```

There is no multipart streaming handler wired into HTTP yet.

### URL OTA

1. User enters firmware URL in web panel OTA page
2. Browser POSTs `{"url": "https://..."}` to `POST /api/ota/url`
3. Firmware uses `esp_https_ota` to fetch and write image
4. HTTPS is required by API validation
5. After complete download and write, image is validated
6. Boot partition is set, device reboots

## Rollback

ESP-IDF provides automatic rollback when `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`:

1. After OTA reboot, the new firmware is in "pending verification" state
2. The firmware must call `esp_ota_mark_app_valid_cancel_rollback()` within
   the watchdog timeout period
3. If the new firmware crashes or fails health checks before marking valid,
   the bootloader rolls back to the previous partition on next reset
4. The health check includes: WiFi connects, event bus initializes, config loads

## Safety Checks

- Image size must be <= OTA partition size (1.5 MB with current table)
- Image must contain valid ESP-IDF app descriptor (magic bytes check)
- OTA is rejected if another OTA is already in progress
- All OTA endpoints require authentication
- OTA state (`idle`/`in_progress`/`validating`/`rebooting`/`failed`) is queryable via `GET /api/ota/status`

## OTA Status Reporting

The `OtaState` enum tracks:
- `idle` — No OTA in progress
- `in_progress` — Downloading/writing firmware
- `validating` — Checking image integrity
- `rebooting` — New image committed, reboot pending
- `failed` — Last OTA attempt failed (with error message)

After reboot, OTA state returns to `idle` after initialization.
Boot-origin/rollback MQTT event payloads are not fully implemented yet and should not be assumed by integrations.
