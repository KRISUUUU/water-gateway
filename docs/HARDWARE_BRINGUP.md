# Hardware Bring-Up

## Scope

First ESP32 + CC1101 board validation: boot, network, radio transport, and
pipeline acceptance of real over-the-air traffic.

## Board Assumptions

- ESP32 module with sufficient flash for the partition table
- CC1101 at 868 MHz, 3.3 V I/O, common ground with ESP32
- Default pins from `board_config` (override in `components/board_config/` if needed):
  - MOSI=23, MISO=19, SCK=18, CS=5, GDO0=4, GDO2=2
- 868 MHz antenna connected

## Bring-Up Sequence

1. **Toolchain** — ESP-IDF v5.x environment active (`export.sh`).
2. **Build** — `idf.py build`.
3. **Flash** — `idf.py -p <PORT> flash` including application and `storage` (SPIFFS).
4. **Monitor** — `idf.py -p <PORT> monitor`; confirm boot reaches app init without panic.
5. **Provisioning vs normal** — Empty Wi‑Fi → AP mode; configured → STA.
6. **Network** — In STA mode: DHCP IP, NTP sync, MQTT connect if configured.
7. **Radio**
   - Logs: CC1101 init, chip ID, RX task running
   - Counters: `frames_received` should move when T-mode traffic is present
   - If `frames_received` increases but UI/MQTT stay empty, see `docs/TROUBLESHOOTING.md` (pipeline rejects: 3-of-6 / L-field / DLL CRC)
8. **Web** — Login, Dashboard, Live Telegrams (newest first), Diagnostics.
9. **MQTT** — Subscribe to `…/telemetry` and `…/rf/raw`; confirm payloads.

## RF / Pipeline Expectations

Firmware applies **SYNC1/SYNC0** (`0x54`/`0x3D`) in the T-mode register block.
The **application pipeline** decodes **3-of-6** and verifies **EN 13757-4** DLL CRC
before exposing a telegram. CC1101 append-status **CRC** bits are separate and
appear in radio diagnostics as `frames_crc_ok` / `frames_crc_fail`.

## API Smoke Tests

```bash
# After login, replace TOKEN and HOST
curl -s -H "Authorization: Bearer TOKEN" http://HOST/api/status
curl -s -H "Authorization: Bearer TOKEN" http://HOST/api/diagnostics/radio
```

## OTA Smoke Test

Use the web UI or `curl` to upload a known-good build; confirm reboot and version.

## When Things Fail

- **No SPI traffic** — Wiring, CS, voltage
- **No frames** — Antenna, frequency, no compatible transmitters nearby
- **Frames received but zero telegrams** — PHY/FIFO format vs `WmbusPipeline` expectations (`docs/LIMITATIONS.md`)
