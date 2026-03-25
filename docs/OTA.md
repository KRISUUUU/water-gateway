# OTA (Over-The-Air) Updates

## Overview

The firmware currently exposes two OTA API paths:

1. **Direct binary upload** — `POST /api/ota/upload` accepts streamed firmware binary
2. **URL OTA** — `POST /api/ota/url` starts OTA from an HTTPS URL

Both methods use ESP-IDF's OTA APIs with rollback support.

## Partition Strategy

| Partition | Size | Purpose |
| --------- | ---- | ------- |
| `ota_0` | 1.5 MB | Active app slot (also used for initial serial flash) |
| `ota_1` | 1.5 MB | Inactive OTA update slot |
| `otadata` | 8 KB | Tracks active boot partition |
| `storage` | 896 KB | SPIFFS web assets / local storage |

OTA writes to the **inactive** OTA partition. After commit, the device reboots
into the newly written partition.

## Update Flow

### Direct Binary Upload

Upload flow:

1. Send firmware as request body to `POST /api/ota/upload`
2. Use header `Content-Type: application/octet-stream`
3. Server streams chunks to OTA partition (`begin_upload` → `write_chunk` → `finalize_upload`)
4. On success, response includes `reboot_required=true`
5. Device should be rebooted to activate new partition

Notes:

- Upload endpoint currently expects raw binary body (not multipart/form-data)
- Upload is rejected when OTA is already in progress (`409`)
- Oversized image is rejected (`413`)

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
- Upload content type must be binary (`application/octet-stream` or `application/x-binary`)

## OTA Status Reporting

The `OtaState` enum tracks:

- `idle` — No OTA in progress
- `in_progress` — Downloading/writing firmware
- `validating` — Checking image integrity
- `rebooting` — New image committed, reboot pending
- `failed` — Last OTA attempt failed (with error message)

After reboot, OTA state returns to `idle` after initialization.
Boot-origin/rollback MQTT event payloads are not fully implemented yet and should not be assumed by integrations.

## UI Notes

- The OTA page groups current state, binary upload, and URL OTA in separate cards.
- UI feedback is explicit for `ota_in_progress`, `image_too_large`, and content-type errors.
- Upload flow remains raw binary body (not multipart form upload).
