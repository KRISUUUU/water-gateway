# Repository Layout

## Top-Level Structure

```
water-gateway/
├── main/           → Application entry point (thin, delegates to app_core)
├── components/     → ESP-IDF components (one per module)
├── web/            → Static web UI assets (HTML, JS, CSS)
├── docs/           → Project documentation
├── tests/          → Host tests and test fixtures
├── .github/        → CI workflows
```

## Component Structure Convention

Every component follows this layout:

```
components/{name}/
├── CMakeLists.txt
├── include/{name}/
│   └── {name}.hpp       (public API header)
└── src/
    └── {name}.cpp       (implementation)
```

Some components expose multiple headers (`config_store`: `config_models.hpp`, `config_validation.hpp`, `config_migration.hpp`, etc.).

## Dependency Layers

Components are organized into dependency layers. A component may only depend on
components in the same layer or lower layers. Never upward.

### Layer 0 — Foundation
- `common` — Shared types, `ErrorCode`, `Result<T>`. No runtime dependencies.

### Layer 1 — Infrastructure
- `event_bus` — In-process publish/subscribe. Depends on: `common`.
- `storage_service` — SPIFFS file abstraction. Depends on: `common`.

### Layer 2 — Configuration
- `config_store` — NVS-backed versioned config (FreeRTOS mutex protects in-memory config). Depends on: `common`, `storage_service`.

### Layer 3 — Platform Services
- `wifi_manager` — WiFi STA/AP, reconnect backoff timer, `std::mutex` for string fields, atomics for state/RSSI/backoff. Depends on: `common`, `event_bus`, `config_store`.
- `ntp_service` — SNTP time sync. Depends on: `common`.
- `mdns_service` — mDNS hostname. Depends on: `common`.
- `provisioning_manager` — First-boot setup. Depends on: `common`, `event_bus`, `config_store`, `wifi_manager`.

### Layer 4 — Domain (Radio, Pipeline, Communication, Auth)

Peers unless explicitly documented.

- `board_config` — Default pins and SPI bus parameters. Depends on: `common`.
- `radio_cc1101` — CC1101 SPI driver, T-mode register table (including `SYNC1`/`SYNC0`). Depends on: `common`.
- `radio_state_machine` — Radio lifecycle FSM. Depends on: `common`, `radio_cc1101`, `event_bus`.
- `wmbus_minimal_pipeline` — 3-of-6 decode, EN 13757 DLL CRC verification, `WmbusFrame`. Depends on: `common`, `radio_cc1101`.
- `dedup_service` — Duplicate frame detection. Depends on: `common`.
- `telegram_router` — Dedup-aware publish vs suppress vs optional event. Depends on: `common`, `dedup_service`, `wmbus_minimal_pipeline`.
- `meter_registry` — Detected meters, watchlist persistence, recent telegrams. Depends on: `common`, `storage_service`, `wmbus_minimal_pipeline`.
- `mqtt_service` — ESP-IDF MQTT client; `std::mutex` around client lifecycle; atomics for counters. Depends on: `common`, `event_bus`, `config_store`, `json`.
- `auth_service` — Authentication and sessions. Depends on: `common`, `config_store`.

### Layer 5 — Presentation, OTA, Observability

- `http_server` — HTTP server wrapper. Depends on: `common`, `auth_service`.
- `api_handlers` — REST API routes. Depends on: `common`, `http_server`, `auth_service`, `config_store`, `mqtt_service`, `diagnostics_service`, `metrics_service`, `health_monitor`, `ota_manager`, `provisioning_manager`, `radio_state_machine`, `meter_registry`, `persistent_log_buffer`, `support_bundle_service`, `json`.
- `web_ui` — Component metadata; static files live under `/web/`.
- `ota_manager` — OTA lifecycle. Depends on: `common`, `event_bus`.
- `diagnostics_service` — Diagnostic aggregation. Depends on: `common`, `radio_cc1101`, `mqtt_service`, `wifi_manager`, `metrics_service`, `health_monitor`, `json`.
- `metrics_service` — Runtime metrics. Depends on: `common`.
- `health_monitor` — Health state (`std::mutex` + `esp_timer` for uptime in snapshot). Depends on: `common`, `esp_timer`.
- `watchdog_service` — Watchdog feeding. Depends on: `common`.
- `persistent_log_buffer` — Log ring buffer. Depends on: `common`.
- `support_bundle_service` — JSON support bundle. Depends on: `common`, `diagnostics_service`, `metrics_service`, `health_monitor`, `config_store`, `persistent_log_buffer`, `meter_registry`, `ota_manager`, `json`.

### Layer 6 — Orchestration
- `app_core` — Boot sequencing, task creation, queue wiring. Depends on: orchestrated components (see `components/app_core/CMakeLists.txt`).

### Layer 7 — Entry
- `main/` — `app_main.cpp`. Depends on: `app_core`.

## Documentation Files

| File | Purpose |
|------|---------|
| `docs/ARCHITECTURE.md` | System architecture, task model, data flow, W-MBus decoding |
| `docs/REPO_LAYOUT.md` | This file |
| `docs/CONFIGURATION.md` | Config model, NVS strategy, import/export |
| `docs/MQTT_TOPICS.md` | MQTT topic and payload contracts |
| `docs/WEB_UI.md` | Web panel pages and API endpoints |
| `docs/OTA.md` | OTA procedures and safety |
| `docs/SECURITY.md` | Threat model and security design |
| `docs/PROVISIONING.md` | First-boot provisioning flow |
| `docs/TESTING.md` | Test strategy and host test guide |
| `docs/OPERATIONS.md` | Operational guidance for deployed devices |
| `docs/TROUBLESHOOTING.md` | Common issues and resolutions |
| `docs/LIMITATIONS.md` | Known limitations and constraints |

## Test Structure

```
tests/
├── README.md               → Test overview and running instructions
├── host/
│   ├── CMakeLists.txt      → Host test build (CMake + CTest)
│   └── test_*.cpp          → Individual test files
└── fixtures/
    ├── README.md           → Fixture format documentation
    └── sample_frames.json  → Sample frame data for reference
```

Host tests compile selected component sources with `HOST_TEST_BUILD` defined.

## Web Assets

```
web/
├── index.html    → Single-page application shell
├── app.js        → UI logic (vanilla JS)
└── styles.css    → Styles
```

Assets are packed into SPIFFS (`storage` partition) at build time and served by the HTTP server.
