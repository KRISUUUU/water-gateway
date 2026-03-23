# OTA (Over-The-Air) Updates

## Overview

The firmware supports two OTA update methods:
1. **Local upload** — Upload a firmware binary via the web panel
2. **URL OTA** — Provide an HTTPS URL from which the firmware downloads the image

Both methods use ESP-IDF's OTA APIs with rollback support.

## Partition Strategy

| Partition | Size | Purpose |
|-----------|------|---------|
| `factory` | 1.5 MB | Initial firmware (serial flash) |
| `ota_0` | 1.5 MB | First OTA slot |
| `ota_1` | 1.5 MB | Second OTA slot |
| `otadata` | 8 KB | Tracks active boot partition |

OTA writes to the **inactive** OTA partition. After commit, the device reboots
into the newly written partition.

## Update Flow

### Local Upload

1. User selects firmware `.bin` file in web panel OTA page
2. Browser POSTs multipart data to `POST /api/ota/upload`
3. Server streams data directly to OTA partition (no full-image RAM buffering)
4. After complete write, image header is validated
5. Boot partition is set to the newly written partition
6. Device reboots

### URL OTA

1. User enters firmware URL in web panel OTA page
2. Browser POSTs `{"url": "https://..."}` to `POST /api/ota/url`
3. Firmware uses `esp_https_ota` to fetch and write image
4. Certificate validation applies if TLS is used
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

- Image size must be <= partition size (1.5 MB)
- Image must contain valid ESP-IDF app descriptor (magic bytes check)
- OTA is rejected if another OTA is already in progress
- OTA is rejected during provisioning mode
- All OTA endpoints require authentication
- OTA state (idle/in_progress/success/failed) is queryable via `GET /api/ota/status`

## OTA Status Reporting

The `OtaState` enum tracks:
- `IDLE` — No OTA in progress
- `IN_PROGRESS` — Writing firmware
- `VALIDATING` — Checking image integrity
- `REBOOTING` — About to reboot
- `FAILED` — Last OTA attempt failed (with error message)

After reboot, the OTA state resets to `IDLE`. The `boot` event published to
MQTT includes whether the boot was from an OTA partition and whether rollback
occurred.
