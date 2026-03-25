# Hardware Bring-Up

## Scope

This checklist is for the first real ESP32 + CC1101 validation run.
It focuses on proving build/boot/radio transport basics, not feature expansion.

## Board Assumptions

- ESP32 dev board with 4 MB flash
- CC1101 module at 868 MHz
- Current default pin profile (`board_config`):
  - MOSI=23
  - MISO=19
  - SCK=18
  - CS=5
  - GDO0=4
  - GDO2=2
- 3.3V power only, common GND between ESP32 and CC1101
- Connected 868 MHz antenna

If your board uses different pins, update `components/board_config/src/board_config.cpp`
before flashing.

## Bring-Up Sequence

1. **Environment**
   - Activate ESP-IDF v5.2 environment.
   - Build firmware: `idf.py build`.
2. **Flash + Monitor**
   - Flash: `idf.py -p <PORT> flash`.
   - Open logs: `idf.py -p <PORT> monitor`.
3. **Foundation Checks**
   - Confirm startup reaches "Foundations initialized".
   - Confirm config is loaded or defaults are persisted.
4. **Mode Check**
   - If WiFi is empty, confirm provisioning mode starts and HTTP server responds.
   - If WiFi is configured, confirm normal runtime starts.
5. **Network Checks (Normal Runtime)**
   - Verify WiFi connection and DHCP IP in logs.
   - Verify MQTT connect/disconnect state transitions in logs.
6. **Radio Checks**
   - Verify CC1101 init log with chip ID.
   - Verify RX task is running (no immediate radio error loop).
   - Verify frame counters change when known WMBus traffic is present.
   - Observe `frames_incomplete` / `frames_dropped_too_long` counters under heavy traffic.
7. **API Checks**
   - Login: `POST /api/auth/login`.
   - Query health/config/diagnostics endpoints with bearer token.
   - Confirm `/api/ota/upload` accepts binary upload and returns `reboot_required`.
   - Confirm `/api/ota/url` rejects non-HTTPS URLs.
8. **Stability Checks (15-30 min)**
   - No crash/reboot loop.
   - Health state is coherent with WiFi/MQTT conditions.
   - Watchdog feed path remains active.

## Expected First Boot Log Sequence

Normal mode (WiFi configured):

1. `=== WMBus Gateway Starting ===`
2. `[BOOT] Foundations: event bus`
3. `[BOOT] Foundations: storage`
4. `[BOOT] Foundations: meter registry`
5. `[BOOT] Foundations: config`
6. `Foundations initialized`
7. `Startup mode selected: normal`
8. `[BOOT] Normal mode: WiFi init + STA start`
9. `[BOOT] Normal mode: NTP init`
10. `[BOOT] Normal mode: mDNS init` (currently expected to run in no-op mode)
11. `[BOOT] Normal mode: MQTT init`
12. `[BOOT] Normal mode: auth init`
13. `[BOOT] Normal mode: HTTP init + handler registration`
14. `[BOOT] Normal mode: OTA init + boot-valid`
15. `[BOOT] Normal mode: watchdog init`
16. `Board CC1101 pins: MOSI=... MISO=... SCK=... CS=... GDO0=... GDO2=...`
17. `CC1101 PARTNUM=... VERSION=...`
18. `Runtime tasks created (...)`
19. `Normal runtime started`

Provisioning mode (WiFi not configured):

1. `=== WMBus Gateway Starting ===`
2. Foundations logs as above
3. `Startup mode selected: provisioning`
4. `[BOOT] Provisioning: starting AP + provisioning manager`
5. `WiFi AP started, SSID: WMBus-GW-Setup`
6. `HTTP server listening on port 80`
7. `Provisioning mode active. Connect to WMBus-GW-Setup AP and open http://192.168.4.1/`

## First Failure Triage

- **No boot / reset loop:** check power integrity, flash size config, and partition table.
- **No CC1101 ID:** verify SPI wiring and CS polarity, then inspect logic analyzer traces.
- **WiFi unstable:** verify antenna/PSU and AP RSSI.
- **MQTT unstable:** verify broker reachability, credentials, and ACLs.
- **No frames:** verify frequency/antenna/nearby transmitter and check CRC/fifo counters.
- **Long frame drops:** inspect `frames_dropped_too_long` and `frames_incomplete`; current polling RX is conservative and may drop oversized/incomplete captures.
- **No HTTP/API:** verify `HTTP server listening on port 80` exists in logs, then check auth login endpoint first.

## Fastest Triage Paths

- **No-radio:** look for `Board CC1101 pins...` and `CC1101 PARTNUM...`; if missing/failing, fix wiring/pins before protocol debugging.
- **No-wifi:** check if mode is `provisioning` vs `normal`; in normal mode validate STA credentials and AP reachability.
- **No-mqtt:** verify WiFi got IP first, then broker URI/port/TLS settings and broker-side ACL/auth.
- **No-http:** if boot reached HTTP stage but no server log, treat as startup failure; if server log exists, test `/api/auth/login` before other endpoints.

## Exit Criteria For First Bring-Up

- Firmware boots and stays stable for at least 15 minutes.
- WiFi and MQTT lifecycle events behave as expected.
- CC1101 initializes and receives at least one frame in live RF conditions.
- Live Telegrams and Detected Meters pages show coherent entries for received traffic.
- API auth + diagnostics endpoints work.
- Known RF limitations are confirmed (`frames_dropped_too_long` behavior in polling mode).
