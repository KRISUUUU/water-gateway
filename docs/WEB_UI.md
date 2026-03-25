# Web Panel

## Overview

The web panel is a simple single-page app (`web/index.html`, `web/app.js`, `web/styles.css`)
served from SPIFFS over HTTP.

It is intended for serviceability and provisioning support, not premium UX.

## Access

- URL: `http://{device_ip}/` (provisioning AP: `http://192.168.4.1/`)
- Port: `80`
- Authentication: required for all `/api/*` endpoints except login

## Available Pages

- Dashboard: health + runtime metrics + WiFi/MQTT/radio summary from `/api/status`
- Live Telegrams: currently placeholder (`/api/telegrams` returns empty list by design)
- RF Diagnostics: radio/RSM counters from `/api/diagnostics/radio`
- MQTT Status: MQTT state/counters from `/api/diagnostics/mqtt`
- Configuration: reads redacted config from `/api/config`, posts updates to `/api/config`
- OTA: shows OTA status, supports URL OTA trigger; local upload is explicitly unavailable
- System: reboot, factory reset, support bundle download
- Logs: recent buffered logs from `/api/logs`

## API Endpoints Used by UI

| Method | Path | Auth | Notes |
| ------ | ---- | ---- | ----- |
| POST | `/api/auth/login` | No | Returns bearer token |
| POST | `/api/auth/logout` | Yes | Invalidates current session |
| GET | `/api/status` | Yes | Mode + health + metrics + WiFi/MQTT/radio summary |
| GET | `/api/telegrams` | Yes | Returns `{"telegrams":[]}` currently |
| GET | `/api/diagnostics/radio` | Yes | RSM + detailed diagnostics snapshot |
| GET | `/api/diagnostics/mqtt` | Yes | MQTT diagnostics |
| GET | `/api/config` | Yes | Redacted config; secrets represented as `***` |
| POST | `/api/config` | Yes | Save config; response includes `reboot_required` |
| GET | `/api/ota/status` | Yes | Includes state/progress/message/current_version |
| POST | `/api/ota/upload` | Yes | Returns `501 not_implemented` |
| POST | `/api/ota/url` | Yes | Requires HTTPS URL |
| GET | `/api/logs` | Yes | Returns `{ "entries": [...] }` |
| GET | `/api/support-bundle` | Yes | Returns support bundle JSON |
| POST | `/api/system/reboot` | Yes | Reboot device |
| POST | `/api/system/factory-reset` | Yes | Reset config + reboot |

## Provisioning Notes

- First boot with empty WiFi config runs AP mode and serves the same UI stack.
- If no admin password hash is configured, login accepts any non-empty password.
- Set `auth.admin_password` in Configuration and save to establish a real admin password hash.
- After save, reboot is required to apply runtime configuration changes predictably.

## Static Asset Delivery

- SPIFFS image is generated from `web/` via `spiffs_create_partition_image(storage web FLASH_IN_PROJECT)`.
- HTTP static handler resolves:
  - `/` -> `/storage/index.html`
  - `/index.html` -> `/storage/index.html`
  - `/app.js` -> `/storage/app.js`
  - `/styles.css` -> `/storage/styles.css`
- Build output includes `build/storage.bin`, and `idf.py flash` writes it to `storage` partition.

## Honest Limitations

- OTA multipart upload is not implemented (endpoint intentionally returns 501).
- Live telegram list in UI is still placeholder until runtime cache/API is added.
- RF correctness/throughput remains hardware-dependent and must be validated on board.
