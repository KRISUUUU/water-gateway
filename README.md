# WMBus Gateway

ESP-IDF firmware for ESP32 + CC1101 — a Wireless M-Bus 868 MHz receiver/gateway
for water meter telegram reception.

## What It Does

- Receives Wireless M-Bus T-mode telegrams on 868.95 MHz via CC1101
- Captures raw frames with RF metadata (RSSI, LQI, CRC status)
- Publishes raw telegrams and telemetry via MQTT
- Provides a built-in web panel for diagnostics, configuration, and service
- Supports OTA firmware updates (local upload + HTTPS URL) with rollback
- Stores config in NVS with validation, migration, and import/export
- Integrates with Home Assistant and external decoders (e.g., wmbusmeters)

## Architecture (Variant B)

The ESP32 + CC1101 is primarily a **robust RF receiver and gateway**.
Heavy meter-specific decoding remains external by default.
See `docs/ARCHITECTURE.md` for the full design.

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

```bash
cd tests/host
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

## Hardware

- ESP32 (4MB flash, any devkit)
- CC1101 868 MHz transceiver module
- SPI wiring: MOSI=23, MISO=19, SCK=18, CS=5, GDO0=4, GDO2=2
- 868 MHz antenna (quarter-wave ~86mm wire or proper PCB antenna)

## Documentation

| Document | Description |
|----------|-------------|
| [Architecture](docs/ARCHITECTURE.md) | System design, task model, data flow |
| [Repository Layout](docs/REPO_LAYOUT.md) | File structure and dependency rules |
| [Configuration](docs/CONFIGURATION.md) | Config model, NVS, import/export |
| [MQTT Topics](docs/MQTT_TOPICS.md) | Topic hierarchy and payload contracts |
| [Web Panel](docs/WEB_UI.md) | Web UI pages and API endpoints |
| [OTA](docs/OTA.md) | Firmware update procedures |
| [Security](docs/SECURITY.md) | Threat model and security design |
| [Provisioning](docs/PROVISIONING.md) | First-boot setup |
| [Testing](docs/TESTING.md) | Test strategy and guide |
| [Operations](docs/OPERATIONS.md) | Operational guidance |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common issues |
| [Limitations](docs/LIMITATIONS.md) | Known constraints |

## Project Status

All core components are implemented:
- Foundation types, event bus, config store with NVS persistence
- WiFi STA/AP, NTP, mDNS, MQTT with reconnect
- CC1101 SPI driver with T-mode register config
- WMBus pipeline with dedup and routing
- Auth service with SHA-256 password hashing
- HTTP server with auth middleware and REST API
- OTA manager with upload and URL modes
- Web panel with dashboard, telegrams, diagnostics, config, OTA, system, logs
- Diagnostics, metrics, health monitoring, watchdog, support bundle
- 7 host test suites, CI pipeline

**Next:** Hardware validation with real CC1101 + water meters.

## License

Private project. Not open source.
