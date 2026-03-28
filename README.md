# WMBus Gateway

ESP-IDF firmware for ESP32 + CC1101 — a Wireless M-Bus 868 MHz receiver/gateway
for water meter telegram reception.

## What It Does

- Receives Wireless M-Bus T-mode on 868.95 MHz via CC1101 (hardware sync word `0x543D`, T-mode register set in `radio_cc1101`)
- Reads RX FIFO into `RawRadioFrame`; the pipeline decodes **Mode-T 3-of-6** symbols and verifies **EN 13757-4** link-layer (DLL) CRC before accepting a frame (invalid captures are dropped; no “fake” meters from random noise at the application layer)
- Presents **decoded link-layer octets** as the canonical `WmbusFrame::raw_bytes`; hex strings are derived for API, MQTT, and UI
- Publishes telegrams and telemetry via MQTT (`cJSON` payloads, RAII-style JSON ownership in services); MQTT topics use `{mqtt.prefix}/{device.hostname}/…`
- Maintains **detected meters**, **watchlist** (alias, note, enabled), and a **recent telegram buffer** for the REST API and web UI
- Serves a built-in web control panel (diagnostics, meters/watchlist, telegrams, configuration, OTA, support bundle, factory reset)
- First-boot **Initial Setup** in provisioning mode (Wi‑Fi + admin password + optional MQTT)
- OTA firmware updates (HTTPS URL and direct binary upload with ESP-IDF rollback)
- Configuration in NVS with validation, migration, and import/export

## Architecture (Variant B)

The ESP32 + CC1101 is primarily a **robust RF receiver and gateway**.
Vendor-specific application-layer decoding stays external by default.
See `docs/ARCHITECTURE.md` for tasks, queues, MQTT path, and W-MBus decoding details.

## Building

### Prerequisites

- [ESP-IDF v5.2+](https://docs.espressif.com/projects/esp-idf/en/latest/)
- CMake 3.16+

### Build

```bash
. $IDF_PATH/export.sh
idf.py build
```

### Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Host Tests

Pure-logic tests (no ESP-IDF flash required):

```bash
cd tests/host
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

Ten executables are registered with CTest (config, dedup, MQTT payloads, auth, health, W-MBus pipeline, meter registry, OTA manager, migration, support summary).

## Hardware

- ESP32 (4MB flash, any devkit)
- CC1101 868 MHz transceiver module
- Default SPI wiring (board profile in `board_config`): MOSI=23, MISO=19, SCK=18, CS=5, GDO0=4, GDO2=2
- 868 MHz antenna (quarter-wave ~86mm wire or PCB antenna)

## Documentation

| Document | Description |
| ---------- | ----------- |
| [Architecture](docs/ARCHITECTURE.md) | System design, task model, data flow, W-MBus path |
| [Repository Layout](docs/REPO_LAYOUT.md) | File structure and dependency rules |
| [Configuration](docs/CONFIGURATION.md) | Config model, NVS, import/export |
| [MQTT Topics](docs/MQTT_TOPICS.md) | Topic hierarchy and payload contracts |
| [Web Panel](docs/WEB_UI.md) | Web UI pages and API endpoints |
| [OTA](docs/OTA.md) | Firmware update procedures |
| [Security](docs/SECURITY.md) | Threat model and security design |
| [Provisioning](docs/PROVISIONING.md) | First-boot setup |
| [Testing](docs/TESTING.md) | Test strategy and guide |
| [Hardware Bring-Up](docs/HARDWARE_BRINGUP.md) | First real board validation checklist |
| [Operations](docs/OPERATIONS.md) | Operational guidance |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common issues |
| [Limitations](docs/LIMITATIONS.md) | Known constraints |
| [Release Readiness](docs/RELEASE_READINESS.md) | Pre-release and post-hardware checklists |

## Project Status

Firmware implements end-to-end **receive → decode → verify → dedup → route → MQTT/API/UI** with:

- **W-MBus pipeline:** 3-of-6 decoding (rtl_433 codeword table), L-field consistency, Format A–style DLL CRC chain (polynomial `0x3D65`, same CRC core as common Wireless M-Bus tools)
- **CC1101:** T-mode tables including `SYNC1`/`SYNC0`, RX FIFO drain with bounded wait, RSSI/LQI/status append, optional hardware CRC status bit in `RawRadioFrame` (independent of DLL CRC used for acceptance)
- **RTOS:** `radio_rx_task` (Core 1), `pipeline_task`, `mqtt_task`, `health_task`; queues `frame_queue` (16× `RawRadioFrame`), `mqtt_outbox` (32× topic + payload); `mqtt_outbox` payload buffer **896** bytes per item
- **Services:** Wi-Fi (STA/AP, mutex + backoff timer), MQTT (ESP-IDF client, `std::mutex` around client handle, atomics for counters), `config_store` (FreeRTOS mutex around config blob), `health_monitor` (`std::mutex` around snapshot)
- **Web:** Vanilla JS SPA; telegrams table sorted newest-first; status badges avoid treating the substring `"connected"` inside `"Disconnected"` as healthy green

**Status:** Core software paths are implemented; **on-air validation** on a specific CC1101 module and local meters remains the operator’s responsibility. Confirm that the PHY/FIFO bytes fed into `WmbusPipeline::from_radio_frame()` match the **even-length 3-of-6 symbol stream** the pipeline expects (see `docs/LIMITATIONS.md`).

**Next:** Hardware validation with real CC1101 + meters; confirm frame counters and Live Telegrams match expected traffic.

## License

Private project. Not open source.
