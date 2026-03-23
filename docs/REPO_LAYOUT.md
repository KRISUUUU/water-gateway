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

Some components have multiple headers or source files when responsibility warrants it
(e.g., `config_store` has `config_models.hpp`, `config_validation.hpp`, `config_migration.hpp`).

## Dependency Layers

Components are organized into dependency layers. A component may only depend on
components in the same layer or lower layers. Never upward.

### Layer 0 — Foundation
- `common` — Shared types, `ErrorCode`, `Result<T>`. No runtime dependencies.

### Layer 1 — Infrastructure
- `event_bus` — In-process publish/subscribe. Depends on: `common`.
- `storage_service` — SPIFFS file abstraction. Depends on: `common`.

### Layer 2 — Configuration
- `config_store` — NVS-backed versioned config. Depends on: `common`, `storage_service`.

### Layer 3 — Platform Services
- `wifi_manager` — WiFi STA management. Depends on: `common`, `event_bus`, `config_store`.
- `ntp_service` — SNTP time sync. Depends on: `common`.
- `mdns_service` — mDNS hostname. Depends on: `common`.
- `provisioning_manager` — First-boot setup. Depends on: `common`, `event_bus`, `config_store`, `wifi_manager`.

### Layer 4 — Domain (Radio, Pipeline, Communication, Auth)

These modules are peers. They do not depend on each other except where
explicitly documented.

- `radio_cc1101` — CC1101 SPI driver. Depends on: `common`.
- `radio_state_machine` — Radio lifecycle FSM. Depends on: `common`, `radio_cc1101`, `event_bus`.
- `wmbus_minimal_pipeline` — Frame-to-metadata conversion. Depends on: `common`, `radio_cc1101`.
- `dedup_service` — Duplicate frame detection. Depends on: `common`.
- `telegram_router` — Frame routing decisions. Depends on: `common`, `dedup_service`, `wmbus_minimal_pipeline`.
- `mqtt_service` — MQTT client lifecycle. Depends on: `common`, `event_bus`, `config_store`.
- `auth_service` — Authentication and sessions. Depends on: `common`, `config_store`.

### Layer 5 — Presentation, OTA, Observability

- `http_server` — HTTP server wrapper. Depends on: `common`, `auth_service`.
- `api_handlers` — REST API routes. Depends on: `common`, `http_server`, `auth_service`, `config_store`, `mqtt_service`, `diagnostics_service`, `metrics_service`, `health_monitor`, `ota_manager`, `radio_state_machine`, `persistent_log_buffer`.
- `web_ui` — Documentation-only component pointing to `/web/` assets.
- `ota_manager` — OTA lifecycle. Depends on: `common`, `event_bus`.
- `diagnostics_service` — Diagnostic aggregation. Depends on: `common`, `radio_cc1101`, `mqtt_service`, `wifi_manager`, `metrics_service`, `health_monitor`.
- `metrics_service` — Runtime metrics (heap, uptime). Depends on: `common`.
- `health_monitor` — Health state machine. Depends on: `common`, `event_bus`.
- `watchdog_service` — Watchdog feeding. Depends on: `common`.
- `persistent_log_buffer` — Log ring buffer. Depends on: `common`.
- `support_bundle_service` — Support bundle export. Depends on: `common`, `diagnostics_service`, `metrics_service`, `health_monitor`, `config_store`.

### Layer 6 — Orchestration
- `app_core` — Boot sequencing, task creation. Depends on: all components.

### Layer 7 — Entry
- `main/` — Contains `app_main.cpp`. Depends on: `app_core` only.

## Documentation Files

| File | Purpose |
|------|---------|
| `docs/ARCHITECTURE.md` | System architecture, task model, data flow |
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
    └── sample_frames.json  → Sample WMBus frame data for replay tests
```

Host tests compile component source files directly (without ESP-IDF) to test
pure logic. Hardware-dependent code is excluded by build-time `#ifdef` or by
testing only the logic portions of each module.

## Web Assets

```
web/
├── index.html    → Single-page application shell
├── app.js        → UI logic (vanilla JS, no framework)
└── styles.css    → Styles
```

Assets are embedded into SPIFFS during build and served by `http_server`.
Total uncompressed size target: < 50KB.
