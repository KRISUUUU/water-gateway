# Troubleshooting

## No Frames Received

**Symptoms:** `frames_received` stays 0, MQTT `rf/raw` is silent, Live Telegrams empty.

**Possible causes:**

1. **CC1101 wiring or SPI** — Verify MOSI/MISO/SCK/CS and power. Chip ID read (PARTNUM) should match the driver’s expectation for CC1101.
2. **Frequency / mode** — Default RF register block is T-mode 868.95 MHz. Meters out of band will not be seen.
3. **Antenna** — Use a suitable 868 MHz antenna or minimal quarter-wave wire (~86 mm).
4. **Radio error state** — RF Diagnostics: high `fifo_overflows`, `radio_resets`, or RSM not in receiving state.
5. **Pipeline vs RF format** — If `frames_received` increments but **nothing** appears in MQTT or Live Telegrams, `WmbusPipeline::from_radio_frame` may be rejecting every capture (3-of-6 decode failure, L-field length mismatch, or DLL CRC failure). Compare with `docs/LIMITATIONS.md` (FIFO vs 3-of-6 contract). Host tests use a synthetic encoded stream; on-device the FIFO must supply the same symbol pairing model.

## Long Frame / FIFO Drops

**Symptoms:** `frames_received` increases slowly while `frames_dropped_too_long` or `frames_incomplete` increases.

**Possible causes:**

1. Polling drain cannot keep up with long packets or bursts.
2. RF environment or CC1101 settings not matching local traffic.
3. FIFO overflow — see `fifo_overflows`.

Use counters as an RF health signal; improving throughput may require interrupt-driven RX or tuning.

## Live Telegrams Shows Empty

**Symptoms:** Dashboard counters move, but the Live Telegrams table is empty.

**Possible causes:**

1. **Filter** — Set filter to `all`; other filters can hide everything.
2. **Session** — 401: log in again.
3. **Recent buffer** — After reboot, history starts empty until new frames arrive.
4. **Pipeline drops** — If frames are received at RF level but **all** fail decode/CRC, the telegram buffer may stay empty while `frames_received` still increases.

## Watchlist / Detected Meters Issues

**Symptoms:** Telegram shows a meter key that does not match watchlist expectations.

**Possible causes:**

1. **Key format** — Use the exact key from Detected Meters (`mfg:...-id:...` or `sig:...`).
2. **Watchlist disabled** — Entry exists but `enabled` is false.
3. **Identity fallback** — Signature-based keys follow decoded frame bytes; different noise before sync can change fallbacks (frames failing the pipeline should not appear in the UI).

## WiFi Connection Issues

**Symptoms:** STA does not connect or reconnects often.

**Possible causes:**

1. Wrong SSID/password — Re-provision or change config.
2. Weak signal — Check RSSI in status or telemetry.
3. **2.4 GHz only** — ESP32 does not use 5 GHz WiFi.

## MQTT Connection Issues

**Symptoms:** Status shows MQTT disconnected; `mqtt_publish_failures` or reconnects climb.

**Possible causes:**

1. Broker host/port wrong — Match `config_store` MQTT section.
2. Auth failure — Username/password.
3. Network unreachable — Routing from device to broker.
4. **Client ID conflict** — Two clients with the same ID force disconnects.

## Web Panel Not Loading

**Symptoms:** Connection refused or blank page.

**Possible causes:**

1. Device not on the network — Wrong IP or WiFi down.
2. **SPIFFS** — Web assets missing: rebuild and flash so `storage` partition contains `index.html`, `app.js`, `styles.css`.
3. Wrong URL — Try `http://<ip>/` or `http://<hostname>.local/`.

## OTA Failed

**Symptoms:** OTA status failed or device boots old firmware.

**Possible causes:**

1. Image too large for OTA partition.
2. Wrong binary (partition layout).
3. URL download interrupted — Retry.
4. New image failed health check — Bootloader rollback.
5. **409** — OTA already in progress.
6. Upload must be raw body (`application/octet-stream`), not multipart.

## Login Rate Limited

**Symptoms:** `POST /api/auth/login` returns `429`.

**Cause:** Too many failed attempts in the window (implementation: 5 failures per 60 seconds).

**Fix:** Wait `retry_after_s` from the JSON body before retrying.

## High Memory Usage

**Symptoms:** `free_heap_bytes` very low.

**Possible causes:**

1. MQTT backlog — Outbox is bounded (32 items); drops are logged when full.
2. Many HTTP clients — Reduce concurrent requests.
3. Leak — Watch `min_free_heap_bytes` over time.

## UI: Status Shows “Disconnected” as Green

**Fixed behavior:** The dashboard badge logic treats error/disconnect states before “healthy” states so **Disconnected** is not classified as green because of the substring **connected**. If you still see wrong colors, hard-refresh the browser to load the current `app.js`.

## Support Bundle

Download from the Support page (authenticated). The JSON bundle includes:

- `diagnostics` (embedded diagnostics snapshot)
- `metrics` (heap, uptime, largest block)
- `health` (state, counts, last warning/error messages)
- `config` (redacted secrets)
- `logs` (recent entries from `persistent_log_buffer`)
- `meters` (detected and watchlist counts)
- `ota` (OTA manager state)

It does **not** include a dedicated reset-reason history table; reset reason may appear in `diagnostics` only if the diagnostics snapshot aggregates it.
