# Operations Guide

## Normal Operation

After provisioning, the device operates autonomously:

1. Connects to configured WiFi network
2. Synchronizes time via NTP
3. Connects to MQTT broker
4. Initializes CC1101 radio in RX mode
5. Receives WMBus frames, deduplicates, and publishes via MQTT
6. Serves web panel on port 80
7. Publishes periodic telemetry (every 30s)

## Monitoring

### MQTT Status Topic

Subscribe to `wmbus-gw/{id}/status` (retained) to monitor disconnect/offline state.
The Last Will ensures the topic shows offline if the device disconnects unexpectedly.
Use telemetry/API as the primary live-state signal in current implementation.

### Telemetry Topic

Subscribe to `wmbus-gw/{id}/telemetry` for periodic metrics:

- `uptime_s`, `free_heap_bytes`, `frames_received`, `mqtt_publishes`
- If `frames_received` stops incrementing, the radio may have an issue
- If `mqtt_failures` is climbing, check broker connectivity
- Track `frames_incomplete` / `frames_dropped_too_long` in radio diagnostics to detect FIFO pressure

### Events Topic

Subscribe to `wmbus-gw/{id}/events` for discrete alerts:

- `radio_error`, `wifi_disconnected`, `mqtt_disconnected`, `ota_*`, `health_degraded`

### Web Panel Dashboard

Access `http://{hostname}.local/` for a visual overview with status badges,
key counters, and quick actions.

### Live Telegrams / Detected Meters / Watchlist

- Use the **Live Telegrams** page to inspect recent frames with filters:
  `all`, `watched`, `unknown`, `duplicates`, `crc_fail`, `problematic` (best-effort).
- Use **Detected Meters** to review observed transmitter identities and counters.
- Use **Detected Meters** actions to add visible meters directly to watchlist flow.
- Use **Watchlist** to assign alias/note and mark important meters as enabled.
- Meter identity is best-effort and based on observed frame fields/signature.

## Common Operational Tasks

### Updating Firmware

1. Open web panel → OTA page
2. Either upload local `.bin` (`/api/ota/upload`) or start HTTPS URL OTA (`/api/ota/url`)
3. Wait for OTA completion status (`reboot_required=true`)
4. Reboot device to activate new partition
5. If verification fails after reboot, bootloader rolls back to previous firmware

### Changing Configuration

1. Open web panel → Settings page
2. Modify fields, click Save
3. Save response indicates reboot requirement
4. If response indicates `relogin_required`, log in again before continuing
5. Reboot from System page for predictable application of runtime changes

### Exporting Configuration

1. Open web panel → Settings page → Export
2. Save JSON file (secrets are redacted)

### Downloading Support Bundle

1. Open web panel → Support page → Download Support Bundle
2. JSON file includes diagnostics, metrics, config (redacted), recent logs, and meter/watchlist counts
   plus build-time security posture flags.
3. Useful for remote troubleshooting

### Factory Reset

1. Open web panel → Factory Reset page
2. Confirm the action
3. Device reboots into provisioning mode

## LED Indicators (If Available)

The firmware does not assume specific LED hardware. If LEDs are connected,
a future GPIO indicator module can signal:

- Solid: normal operation
- Slow blink: provisioning mode
- Fast blink: error state

## Power Considerations

- The device should be powered via stable USB or regulated 3.3V supply
- Brown-out detection is enabled by default in ESP-IDF
- Unexpected power loss may cause NVS write corruption in rare cases;
  the config store uses atomic NVS operations to minimize this risk
