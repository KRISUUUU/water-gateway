# Web Panel

## Overview

The web panel is a single-page control panel (`web/index.html`, `web/app.js`,
`web/styles.css`) served from SPIFFS over HTTP port 80.

It is designed for day-to-day gateway operation, provisioning, and serviceability,
without a heavy frontend framework.

## Access

- URL: `http://{device_ip}/` (provisioning AP: `http://192.168.4.1/`)
- Port: `80`
- Authentication: required for management APIs; `/api/bootstrap` and `/api/auth/login` are unauthenticated for startup flow

## Navigation and Pages

The shell uses a sidebar on desktop and a collapsible menu on small screens.

Sections: Dashboard, Live Telegrams, Detected Meters, Watchlist, Diagnostics,
Logs, OTA, Settings, Support, Factory Reset.

### Dashboard

- Mode, health, firmware, Wi‑Fi / MQTT / radio badges
- Counters: frames received, CRC fail (radio hardware counter), duplicates, incomplete, dropped-too-long, MQTT failures, heap, RSSI
- Detected meters and watchlist counts
- Quick links (meters, OTA, support) and reboot shortcut

**Refresh:** While the dashboard is open, `/api/status` is polled on a short interval; full meter/watchlist refresh uses a slower interval.

### Live Telegrams

- Table: timestamp, meter key, alias, raw hex, length, RSSI, LQI, CRC OK, duplicate, watched
- Filters: `all`, `watched`, `unknown`, `duplicates`, `crc_fail`, `problematic` (maps to CRC-fail oriented server filter where applicable)
- **Sort:** Rows are sorted by `timestamp_ms` descending (newest first) after each load
- Row actions: **Copy** (copies hex; button shows “Copied!” and is disabled briefly), **Add Watch** / **Edit Watch**

### Detected Meters

- Inventory with filters and search (alias / key)
- Actions to open watchlist editor with key prefilled

### Watchlist

- Form: `key`, `alias`, `note`, `enabled`
- Table rows click to load the form

### Diagnostics

- Radio, MQTT, system, OTA snapshot cards (structured key/value)

### Logs

- Severity filter and refresh; lines from `persistent_log_buffer`

### OTA

- Status, upload, URL trigger

### Settings

- Sectioned config editor with save, export, validation messages (`reboot_required`, `relogin_required`)

### Initial Setup (provisioning first boot)

- Shown when bootstrap reports provisioning with no password set
- Collects Wi‑Fi, admin password, optional MQTT and device identity

### Support

- Support bundle download and short runtime summary

### Factory Reset

- Destructive reset with confirmation

## API Endpoints Used by UI

| Method | Path | Auth | Notes |
| ------ | ---- | ---- | ----- |
| GET | `/api/bootstrap` | No | `{ mode, provisioning, password_set }` |
| POST | `/api/auth/login` | No | Bearer token |
| POST | `/api/auth/logout` | Yes | |
| POST | `/api/auth/password` | Yes | Change password |
| GET | `/api/status` | Yes | Aggregated status |
| GET | `/api/telegrams` | Yes | `?filter=` optional |
| GET | `/api/meters/detected` | Yes | `?filter=` optional |
| GET | `/api/watchlist` | Yes | |
| POST | `/api/watchlist` | Yes | Upsert |
| POST | `/api/watchlist/delete` | Yes | Delete by key |
| GET | `/api/diagnostics/radio` | Yes | |
| GET | `/api/diagnostics/mqtt` | Yes | |
| GET | `/api/config` | Yes | Redacted |
| POST | `/api/config` | Yes | Save |
| GET | `/api/ota/status` | Yes | |
| POST | `/api/ota/upload` | Yes | Binary body |
| POST | `/api/ota/url` | Yes | HTTPS URL JSON |
| GET | `/api/logs` | Yes | |
| GET | `/api/support-bundle` | Yes | JSON download |
| POST | `/api/system/reboot` | Yes | |
| POST | `/api/system/factory-reset` | Yes | |

## Status Badge Behavior

Status strings are classified for badge color with **disconnect-related** states
handled before generic “connected” / “ok” matching, so labels like **Disconnected**
are not shown as green due to substring matching.

## Static Asset Delivery

SPIFFS image is generated from `web/` in the ESP-IDF project. Flash the `storage`
partition so `index.html`, `app.js`, and `styles.css` are available.

## Honest Limitations

- OTA upload expects raw binary body, not multipart.
- Meter identity is derived from decoded link-layer fields; see architecture docs.
- RF performance depends on hardware and antenna.
