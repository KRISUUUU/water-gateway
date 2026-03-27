# Operations Guide

## Normal Operation

After provisioning, the device:

1. Connects Wi‑Fi STA (or shows provisioning AP if unconfigured)
2. Syncs time via SNTP when connected
3. Connects to MQTT if enabled and configured
4. Initializes CC1101 and the radio state machine; RX task polls for frames
5. Pipeline task decodes 3-of-6, verifies DLL CRC, deduplicates, routes to MQTT, updates `meter_registry`
6. Serves the web panel on port 80
7. Publishes telemetry about every **30 seconds** when MQTT is connected (`health_task`)

## Monitoring

### MQTT status topic

Retained `{prefix}/{hostname}/status` shows online/offline (Last Will covers abrupt disconnect). Use the configured `device.hostname` as the middle path segment.

### Telemetry topic

`{prefix}/{hostname}/telemetry` includes `uptime_s`, heap, Wi‑Fi RSSI, MQTT/radio state strings,
`frames_received`, publish and duplicate counters, **radio** `frames_crc_fail`
(CC1101 hardware/status counter), and MQTT publish counters.

Use `frames_received` vs published/duplicate counts to spot drops or dedup behavior.

### Events topic

Discrete alerts (radio recovery, OTA, etc.) when emitted by firmware.

### Web dashboard

Visual overview: health, mode, firmware, key counters, badges.

**Badge semantics:** “Disconnected” / error states are not styled as healthy green (see `docs/WEB_UI.md`).

### Live Telegrams / meters / watchlist

- **Live Telegrams:** Newest frames first (sorted by `timestamp_ms`). Filters narrow the API result.
- **Detected Meters:** Observed identities and traffic counters.
- **Watchlist:** Alias, note, enabled flag for important meters.

## Common Operational Tasks

### Firmware update

1. Web → OTA: upload `.bin` or start HTTPS URL OTA
2. Wait for success / `reboot_required`
3. Reboot if required
4. On failure, rollback may restore the previous partition

### Configuration change

Settings → save. Some changes require reboot; auth changes may require re-login (`relogin_required`).

### Support bundle

Support page → download JSON. Contains diagnostics, metrics, health, redacted config, logs, meter counts, OTA state (see `support_bundle_service`).

### Factory reset

Factory Reset page → confirm. Erases config and reboots into provisioning-style behavior when Wi‑Fi is cleared.

## Security Reminders

- Change default / initial passwords promptly
- Use MQTT TLS when crossing untrusted networks
- Keep firmware updated

## Logs

`/api/logs` exposes the in-RAM buffer; entries are lost on reboot.
