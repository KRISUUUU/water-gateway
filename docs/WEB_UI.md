# Web Panel

## Overview

The web panel is a modern single-page control panel (`web/index.html`,
`web/app.js`, `web/styles.css`) served from SPIFFS over HTTP.

It is designed for day-to-day gateway operation, provisioning, and serviceability,
while remaining lightweight and embedded-friendly (no heavy frontend framework).

## Access

- URL: `http://{device_ip}/` (provisioning AP: `http://192.168.4.1/`)
- Port: `80`
- Authentication: required for management endpoints; unauthenticated bootstrap/login endpoints are used only for startup/auth flow

## Navigation and Pages

The panel uses a persistent navigation shell:

- Desktop: left sidebar
- Mobile: top bar with collapsible menu

Main sections:

- Dashboard
- Live Telegrams
- Detected Meters
- Watchlist
- Diagnostics
- Logs
- OTA
- Settings
- Support
- Factory Reset

### Dashboard

- Health + mode + firmware summary
- WiFi/MQTT/radio status badges
- Counters: frames, CRC fail, duplicates (best-effort), incomplete, dropped-too-long,
  publish failures
- Detected/watchlist counts
- Quick actions (jump to meters/OTA/support, reboot shortcut)

### Live Telegrams

- Recent frame table with metadata (timestamp, key, alias, raw hex, RSSI/LQI, CRC,
  duplicate/watched flags, length)
- Filters: `all`, `watched`, `unknown`, `duplicates`, `crc_fail`,
  `problematic` (best-effort, mapped to CRC-fail scope)
- Row actions: copy raw frame, jump to watchlist editor

### Detected Meters

- Inventory view with identity and signal/traffic counters
- Filters + search (`alias`/`key`)
- Action to add/edit watchlist entry

### Watchlist

- Form-based edit (`key`, `alias`, `note`, `enabled`) + list table
- Click row to prefill form for edits

### Diagnostics

- Grouped cards for radio, MQTT, system health/memory, OTA state
- Structured key/value display (not raw JSON dump)

### Logs

- Severity filter + refresh
- Readable log stream rendering

### OTA

- Current OTA state/progress/message/version
- Binary upload trigger
- URL OTA trigger (HTTPS only)

### Settings

- Sectioned config editor (Device/WiFi/MQTT/Radio/Auth/Logging)
- Validation/success/relogin/reboot messaging
- Used after onboarding and in normal operations

### Initial Setup (Provisioning First Boot)

- Shown when `/api/bootstrap` reports `provisioning=true` and `password_set=false`
- Replaces normal sign-in prompt on first boot
- Collects minimum onboarding fields:
  - WiFi SSID
  - WiFi password
  - admin password
- Optional fields:
  - device name/hostname
  - MQTT section (enabled via toggle)
- On success, UI shows explicit reboot/apply guidance

### Support

- Support bundle download
- Compact runtime summary

### Factory Reset

- Dedicated danger zone with clear warning
- Separate reboot-only and full factory-reset actions

## API Endpoints Used by UI

| Method | Path | Auth | Notes |
| ------ | ---- | ---- | ----- |
| GET | `/api/bootstrap` | No | Returns `{ mode, provisioning, password_set, provisioning_ap_open, bootstrap_login_open, provisioning_insecure_window }` for startup UX routing and security hints |
| POST | `/api/auth/login` | No | Returns bearer token |
| POST | `/api/auth/logout` | Yes | Invalidates current session |
| POST | `/api/auth/password` | Yes | Change admin password (requires current password when already set) |
| GET | `/api/status` | Yes | Small dashboard status: mode + health + WiFi/MQTT/radio summary + basic metrics/time |
| GET | `/api/status/full` | Yes | Pełne dane diagnostyczne (kolejki, stack, config store, security) |
| GET | `/api/telegrams` | Yes | Recent telegrams; optional `?filter=watched\|unknown\|duplicates\|crc_fail` |
| GET | `/api/meters/detected` | Yes | Detected meter model; optional `?filter=watched\|unknown` |
| GET | `/api/watchlist` | Yes | Watchlist entries |
| POST | `/api/watchlist` | Yes | Upsert watchlist entry (`key`, `alias`, `note`, `enabled`) |
| POST | `/api/watchlist/delete` | Yes | Remove watchlist entry (`key`) |
| GET | `/api/diagnostics/radio` | Yes | RSM + detailed diagnostics snapshot |
| GET | `/api/diagnostics/mqtt` | Yes | MQTT diagnostics |
| GET | `/api/config` | Yes | Redacted config; secrets represented as `***` |
| POST | `/api/config` | Yes | Save config; response includes `reboot_required` |
| GET | `/api/ota/status` | Yes | Includes state/progress/message/current_version |
| POST | `/api/ota/upload` | Yes | Streamed binary upload (`application/octet-stream`), returns `reboot_required` |
| POST | `/api/ota/url` | Yes | Requires HTTPS URL |
| GET | `/api/logs` | Yes | Returns `{ "entries": [...] }` |
| GET | `/api/support-bundle` | Yes | Returns support bundle JSON |
| POST | `/api/system/reboot` | Yes | Reboot device |
| POST | `/api/system/factory-reset` | Yes | Reset config + reboot |

## Provisioning Notes

- First boot with empty WiFi config runs AP mode and serves the same UI stack.
- UI startup calls `/api/bootstrap` to choose between:
  - Initial Setup (first boot, no admin hash)
  - Normal Sign In (already configured auth)
- Backend compatibility note: if no admin hash exists, passwordless login is allowed only in provisioning mode.
- UI keeps first boot on Initial Setup and avoids exposing passwordless bootstrap as a generic login step.
- After initial setup save, reboot is required before normal operations.

## Static Asset Delivery

- SPIFFS image is generated from `web/` via `spiffs_create_partition_image(storage web FLASH_IN_PROJECT)`.
- HTTP static handler resolves:
  - `/` -> `/storage/index.html`
  - `/index.html` -> `/storage/index.html`
  - `/app.js` -> `/storage/app.js`
  - `/styles.css` -> `/storage/styles.css`
- Build output includes `build/storage.bin`, and `idf.py flash` writes it to `storage` partition.

## Honest Limitations

- OTA upload endpoint expects raw binary body (not multipart form-data).
- Detected meter identity is best-effort: it uses `manufacturer_id` + `device_id`
  when present in the observed frame layout, otherwise a stable signature prefix.
- RF correctness/throughput remains hardware-dependent and must be validated on board.
