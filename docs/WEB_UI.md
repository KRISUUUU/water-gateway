# Web Panel

## Overview

The built-in web panel provides service, diagnostics, and configuration access
for the gateway. It is a single-page application built with vanilla HTML, CSS,
and JavaScript — no frameworks, no build step, no npm dependencies.

## Access

- **URL:** `http://{device_ip}/` or `http://{hostname}.local/`
- **Port:** 80 (HTTP)
- **Authentication:** Required for all API access (login form on first visit)

## Pages

### Dashboard
- Device health state (healthy/warning/error)
- Uptime, firmware version, IP address
- Key counters: frames received, frames published, MQTT publishes
- WiFi RSSI, MQTT connection state, radio state

### Live Telegrams
- Table of recently received frames (last 50)
- Columns: timestamp, raw hex (truncated), RSSI, LQI, CRC status, length
- Auto-refreshes every 5 seconds
- Click to expand full hex

### RF Diagnostics
- Radio state (idle/RX/error)
- Frame reception counters
- CRC pass/fail counts
- FIFO overflow count
- Radio reset/recovery count
- RSSI range of recent frames

### MQTT Status
- Connection state
- Broker host:port
- Publish counters (success/failure)
- Reconnect count
- Last publish timestamp

### Configuration
- Form-based editor for all config sections
- Secret fields shown as `***` (can be updated by entering new value)
- Validate button (client-side + server-side)
- Save button (applies and persists)
- Export/Import buttons (JSON)

### OTA
- Local upload: file picker + upload button + progress bar
- URL OTA: URL input + trigger button
- Current firmware version display
- OTA status (idle/in_progress/failed/success)
- Rollback status after reboot

### System
- Reboot button
- Factory reset button (with confirmation)
- Support bundle download button
- Device identity info (MAC, hostname, firmware)

### Logs
- Recent log lines from persistent buffer (last 200)
- Severity filtering (error/warning/info/debug)
- Auto-refresh toggle

## API Endpoints

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| POST | `/api/auth/login` | No | Login, returns session token |
| POST | `/api/auth/logout` | Yes | Invalidate session |
| GET | `/api/status` | Yes | Dashboard data |
| GET | `/api/telegrams` | Yes | Recent frames |
| GET | `/api/diagnostics/radio` | Yes | RF diagnostics |
| GET | `/api/diagnostics/mqtt` | Yes | MQTT diagnostics |
| GET | `/api/config` | Yes | Current config (redacted) |
| POST | `/api/config` | Yes | Update config |
| POST | `/api/config/export` | Yes | Export config JSON |
| POST | `/api/config/import` | Yes | Import config JSON |
| GET | `/api/ota/status` | Yes | OTA state |
| POST | `/api/ota/upload` | Yes | Upload firmware binary |
| POST | `/api/ota/url` | Yes | Trigger URL-based OTA |
| GET | `/api/logs` | Yes | Log buffer contents |
| GET | `/api/support-bundle` | Yes | Download support bundle |
| POST | `/api/system/reboot` | Yes | Trigger reboot |
| POST | `/api/system/factory-reset` | Yes | Factory reset |

## Asset Delivery

Static files (`index.html`, `app.js`, `styles.css`) are stored on the SPIFFS
partition and served by the HTTP server. The SPIFFS image is built during the
firmware build and flashed alongside the application.

## Design Constraints

- Total asset size < 50 KB (SPIFFS has 1.375 MB but we aim for fast load)
- No external CDN dependencies (fully self-contained)
- Works on mobile browsers (responsive CSS)
- No WebSocket (polling-based updates to keep server simple)
- Refresh intervals: 5s for telegrams, 10s for dashboard, 30s for diagnostics
