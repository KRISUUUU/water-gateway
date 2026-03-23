# Troubleshooting

## No Frames Received

**Symptoms:** `frames_received` counter stays at 0, no messages on `rf/raw` topic.

**Possible Causes:**
1. **CC1101 not connected or SPI wiring wrong** — Check SPI connections (MOSI, MISO, SCK, CS, GDO0). Verify CC1101 responds by reading chip part number register (0x30 should return 0x00 for CC1101).
2. **Wrong frequency** — Default is 868.950 MHz (T-mode). Verify meters in range use this frequency. Some meters use C-mode or different T-mode variants.
3. **Antenna missing or poor** — CC1101 needs an appropriate 868 MHz antenna. A wire cut to ~86mm (quarter-wave) is a minimum.
4. **Radio in error state** — Check web panel RF Diagnostics page. If radio state is "error", check `fifo_overflows` and `radio_resets` counters.

## WiFi Connection Issues

**Symptoms:** Device fails to connect, frequent reconnects.

**Possible Causes:**
1. **Wrong credentials** — Re-provision or update via serial console
2. **Weak signal** — Move device closer to AP, or check WiFi RSSI in telemetry
3. **AP compatibility** — ESP32 supports 2.4 GHz only (no 5 GHz)

## MQTT Connection Issues

**Symptoms:** `mqtt_state` shows "disconnected", `mqtt_reconnects` climbing.

**Possible Causes:**
1. **Wrong broker address/port** — Verify config matches broker
2. **Authentication failure** — Verify MQTT username/password
3. **Broker unreachable** — Check network routing from device to broker
4. **Client ID conflict** — Two devices with same client ID cause disconnects

## Web Panel Not Loading

**Symptoms:** Browser shows connection refused or blank page.

**Possible Causes:**
1. **Device not connected to WiFi** — Cannot reach device IP
2. **SPIFFS not flashed** — Web assets may not be on the flash partition; reflash
3. **Wrong IP/hostname** — Use `wmbus-gw.local` or check router for assigned IP

## OTA Failed

**Symptoms:** OTA status shows "failed" or device doesn't boot new firmware.

**Possible Causes:**
1. **Image too large** — Must be < 1.5 MB (partition size)
2. **Wrong image** — Must be built for this specific ESP32 partition layout
3. **Download interrupted** — URL OTA failed mid-download; retry
4. **Rollback occurred** — New firmware failed health check; device reverted

## High Memory Usage

**Symptoms:** `free_heap_bytes` drops below 50KB, potential crashes.

**Possible Causes:**
1. **MQTT backlog** — If broker is unreachable, outbox may grow; bounded at 32 items
2. **Many concurrent HTTP requests** — Web panel open in multiple tabs
3. **Memory leak** — Check `min_free_heap_bytes` trend over time

## Support Bundle

For remote troubleshooting, download the support bundle from System page.
It contains:
- Device identity and firmware version
- Health state and all diagnostic counters
- Configuration (secrets redacted)
- Recent log lines
- Reset reason history
