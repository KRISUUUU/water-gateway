# Architecture

## 1. System Context

```
┌─────────────────────────────────────────────────────────────────┐
│                     External Systems                            │
│                                                                 │
│  ┌──────────┐  ┌────────────┐  ┌──────────────┐  ┌──────────┐ │
│  │  MQTT     │  │  Home      │  │ wmbusmeters  │  │  OTA     │ │
│  │  Broker   │  │ Assistant  │  │ / decoder    │  │  Server  │ │
│  └────▲─────┘  └─────▲──────┘  └──────▲───────┘  └────▲─────┘ │
│       │              │                │                │        │
└───────┼──────────────┼────────────────┼────────────────┼────────┘
        │ MQTT         │ via MQTT       │ via MQTT       │ HTTPS
        │              │                │                │
┌───────┴──────────────┴────────────────┴────────────────┴────────┐
│                    ESP32 + CC1101 Gateway                        │
│                                                                 │
│  ┌───────────┐  ┌──────────┐  ┌────────────┐  ┌────────────┐  │
│  │  CC1101   │  │  WiFi    │  │  Web Panel │  │  NVS       │  │
│  │  868 MHz  │  │  STA     │  │  :80       │  │  Config    │  │
│  │  SPI bus  │  │          │  │            │  │            │  │
│  └───────────┘  └──────────┘  └────────────┘  └────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

The ESP32 + CC1101 acts as a **Wireless M-Bus 868 MHz RF gateway**. It receives
raw telegrams over the air, attaches metadata (RSSI, LQI, timestamp, CRC status),
performs deduplication, and publishes outbound via MQTT. A built-in web panel
provides service/diagnostics access. Heavy vendor-specific decoding is delegated
to external systems.

## 2. Language Choice: C++

**Decision:** C++ (C++17, as supported by ESP-IDF v5.x toolchain).

**Rationale:**
- `Result<T>` template eliminates error-handling boilerplate and prevents silent failures
- RAII for resource management (SPI bus handles, NVS handles, HTTP server)
- Namespaces enforce module isolation at compile time
- Classes with explicit constructors prevent uninitialized state
- The existing codebase is already C++; switching would be wasteful
- ESP-IDF's C APIs are callable from C++ without wrappers

**Constraints:**
- No exceptions (disabled in ESP-IDF by default; use `Result<T>` instead)
- No RTTI (disabled by default; not needed)
- Minimal STL use in hot paths (radio RX); STL acceptable in config/HTTP/diagnostics paths
- `std::string` acceptable in config/HTTP/diagnostics paths; JSON serialization uses `cJSON` with RAII wrappers in service/API layers
- No dynamic allocation in ISR context

## 3. Module Responsibility Map

### Foundation Layer (no ESP-IDF runtime deps for core types)

| Module | Responsibility |
|--------|---------------|
| `common` | Shared types, `ErrorCode` enum, `Result<T>`, timestamp aliases |
| `event_bus` | In-process publish/subscribe for loosely-coupled system events |

### Configuration Layer

| Module | Responsibility |
|--------|---------------|
| `config_store` | Versioned config model, NVS persistence, validation, migration, import/export |
| `storage_service` | SPIFFS filesystem abstraction for web assets and support bundles |

### Platform Services Layer

| Module | Responsibility |
|--------|---------------|
| `wifi_manager` | WiFi STA lifecycle, reconnect with backoff, state reporting |
| `ntp_service` | SNTP time synchronization |
| `mdns_service` | mDNS hostname advertisement |
| `provisioning_manager` | First-boot setup mode (AP + captive portal or serial provisioning) |

### Board Profile Layer

| Module | Responsibility |
|--------|---------------|
| `board_config` | Board-specific wiring defaults (e.g., CC1101 SPI/GDO pins), isolated from orchestration |

### Radio Layer

| Module | Responsibility |
|--------|---------------|
| `radio_cc1101` | CC1101 SPI driver: register config, reset, RX mode, FIFO read, recovery |
| `radio_state_machine` | Radio lifecycle: init → RX → error → recovery, with state tracking |

### Pipeline Layer

| Module | Responsibility |
|--------|---------------|
| `wmbus_minimal_pipeline` | Raw frame → WmbusFrame with metadata (RSSI, LQI, CRC, timestamp, hex) |
| `dedup_service` | Sliding-window duplicate detection by frame content hash |
| `telegram_router` | Route decisions: publish raw, suppress duplicate, flag bad CRC |

### Communication Layer

| Module | Responsibility |
|--------|---------------|
| `mqtt_service` | MQTT client lifecycle, connection, reconnect, publish, Last Will |
| `mqtt_topics` | Topic string generation from config prefix + device ID |
| `mqtt_payloads` | JSON payload serialization for each topic category |

### Web/API Layer

| Module | Responsibility |
|--------|---------------|
| `auth_service` | Session management, login/logout, token validation, password verification |
| `http_server` | ESP-IDF HTTP server wrapper, URI registration, static file serving |
| `api_handlers` | REST API route handlers for all panel features |
| `web_ui` | Static HTML/JS/CSS assets served from SPIFFS |

### OTA Layer

| Module | Responsibility |
|--------|---------------|
| `ota_manager` | OTA lifecycle: begin, write, validate, commit, rollback |
| `ota_state` | OTA state tracking and boot health confirmation |

### Observability Layer

| Module | Responsibility |
|--------|---------------|
| `diagnostics_service` | Aggregates cross-system diagnostic snapshot |
| `metrics_service` | Runtime metrics: uptime, heap, task watermarks |
| `health_monitor` | System health state machine: starting → healthy → warning → error |
| `watchdog_service` | Hardware watchdog integration and task-level feeding |
| `persistent_log_buffer` | In-memory ring buffer for recent log lines (RAM-backed) |
| `support_bundle_service` | Generates redacted JSON support bundle |

### Orchestration Layer

| Module | Responsibility |
|--------|---------------|
| `app_core` | Boot sequencing, mode selection, service initialization, runtime task orchestration |

## 4. Dependency Rules

### Principles

1. **Downward-only dependencies.** Higher layers depend on lower layers, never the reverse.
2. **No circular dependencies.** If A depends on B, B must not depend on A.
3. **Event bus for cross-cutting notifications.** Modules that need to react to other modules' state changes subscribe to events rather than taking direct dependencies.
4. **Common is universal.** Every module may depend on `common`.

### Dependency Graph

```
Layer 0 (Foundation):     common
                            ↑
Layer 1 (Infrastructure):  event_bus, storage_service
                            ↑
Layer 2 (Config):          config_store
                            ↑
Layer 3 (Platform):        wifi_manager, ntp_service, mdns_service, provisioning_manager
                            ↑
Layer 4a (Radio):          radio_cc1101, radio_state_machine
Layer 4b (Pipeline):       wmbus_minimal_pipeline, dedup_service, telegram_router
Layer 4c (Comms):          mqtt_service (mqtt_topics, mqtt_payloads)
Layer 4d (Auth):           auth_service
                            ↑
Layer 5 (Presentation):   http_server, api_handlers, web_ui
Layer 5 (OTA):             ota_manager, ota_state
Layer 5 (Observability):  diagnostics_service, metrics_service, health_monitor,
                           watchdog_service, persistent_log_buffer, support_bundle_service
                            ↑
Layer 6 (Orchestration):   app_core
                            ↑
Layer 7 (Entry):           main/app_main.cpp
```

### Specific Component Dependencies

| Component | REQUIRES |
|-----------|----------|
| `common` | (none) |
| `event_bus` | `common` |
| `storage_service` | `common` |
| `config_store` | `common`, `storage_service` |
| `wifi_manager` | `common`, `event_bus`, `config_store` |
| `ntp_service` | `common` |
| `mdns_service` | `common` |
| `provisioning_manager` | `common`, `event_bus`, `config_store`, `wifi_manager` |
| `radio_cc1101` | `common` |
| `radio_state_machine` | `common`, `radio_cc1101`, `event_bus` |
| `wmbus_minimal_pipeline` | `common`, `radio_cc1101` |
| `dedup_service` | `common` |
| `telegram_router` | `common`, `dedup_service`, `wmbus_minimal_pipeline` |
| `mqtt_service` | `common`, `event_bus`, `config_store`, `json` |
| `auth_service` | `common`, `config_store` |
| `http_server` | `common`, `auth_service` |
| `api_handlers` | `common`, `http_server`, `auth_service`, `config_store`, `mqtt_service`, `diagnostics_service`, `metrics_service`, `health_monitor`, `ota_manager`, `radio_state_machine`, `persistent_log_buffer`, `support_bundle_service`, `json` |
| `ota_manager` | `common`, `event_bus` |
| `diagnostics_service` | `common`, `radio_cc1101`, `mqtt_service`, `wifi_manager`, `metrics_service`, `health_monitor`, `json` |
| `metrics_service` | `common` |
| `health_monitor` | `common`, `esp_timer` |
| `watchdog_service` | `common` |
| `persistent_log_buffer` | `common` |
| `support_bundle_service` | `common`, `diagnostics_service`, `metrics_service`, `health_monitor`, `config_store`, `persistent_log_buffer`, `meter_registry`, `ota_manager`, `json` |
| `app_core` | All components (orchestrator) |

## 5. FreeRTOS Task Model

### Task Table

| Task | Core | Priority | Stack | Purpose |
|------|------|----------|-------|---------|
| `main` (app_main) | 0 | 1 (default) | 3584B (default) | Init sequence, then deletes itself |
| `radio_rx_task` | 1 | 10 (high) | 4096B | CC1101 SPI poll, FIFO read, enqueue raw frames |
| `pipeline_task` | 0 | 7 (medium-high) | 4096B | Frame processing, dedup, routing, enqueue MQTT messages |
| `mqtt_task` | 0 | 5 (medium) | 6144B | MQTT connection management and publishing |
| `health_task` | 0 | 3 (low) | 4096B | Periodic health checks, watchdog feeding, telemetry publish |
| `httpd` (internal) | 0 | 5 | 4096B | ESP-IDF HTTP server (created by httpd_start) |

### Design Decisions

- **Radio on Core 1:** Isolates time-sensitive SPI polling from WiFi/MQTT/HTTP activity on Core 0. Prevents WiFi stack latency from causing FIFO overflow.
- **No ISR for CC1101 GDO:** While CC1101 supports GDO interrupt pins, the initial design uses polling for simplicity and debuggability. GDO-interrupt mode is a documented future optimization path.
- **app_main deletes itself:** After init, the main task creates worker tasks and exits. No wasted stack sitting idle.
- **mqtt_task has larger stack:** JSON serialization and TLS (if enabled) require more stack.

### Priority Rationale

Radio RX is highest because FIFO overflow means lost frames. Pipeline is next because it feeds the publish path. MQTT and HTTP are equal medium priority. Health is lowest because it's periodic and non-urgent.

## 6. Event/Queue Model

### FreeRTOS Queues (Data Flow)

| Queue | Item Type | Depth | Producer | Consumer |
|-------|-----------|-------|----------|----------|
| `frame_queue` | `RawRadioFrame` | 16 | `radio_rx_task` | `pipeline_task` |
| `mqtt_outbox` | `MqttOutboxItem` | 32 | `pipeline_task`, `health_task` | `mqtt_task` |

`RawRadioFrame`: ~280 bytes max (raw bytes array + length + RSSI + LQI + CRC status).
`MqttOutboxItem`: Fixed-size struct with `char topic[128]` + `char payload[640]` (passed by value through the queue, no heap allocation).

### Event Bus (Notifications)

The event bus carries lightweight state-change notifications. Handlers run synchronously
in the caller's context, so they must be fast (no blocking, no heavy work).

| Event Type | Payload | Publishers | Typical Subscribers |
|------------|---------|-----------|-------------------|
| `WIFI_CONNECTED` | IP address | `wifi_manager` | `mqtt_service`, `ntp_service`, `mdns_service` |
| `WIFI_DISCONNECTED` | reason code | `wifi_manager` | `mqtt_service`, `health_monitor` |
| `MQTT_CONNECTED` | — | `mqtt_service` | `health_monitor`, `diagnostics_service` |
| `MQTT_DISCONNECTED` | reason | `mqtt_service` | `health_monitor` |
| `RADIO_ERROR` | error code | `radio_state_machine` | `health_monitor`, `diagnostics_service` |
| `RADIO_RECOVERED` | — | `radio_state_machine` | `health_monitor` |
| `CONFIG_CHANGED` | — | `config_store` | `mqtt_service`, `wifi_manager` (reconnect if creds changed) |
| `OTA_STARTED` | — | `ota_manager` | `health_monitor`, web UI (via polling) |
| `OTA_COMPLETED` | success/fail | `ota_manager` | `health_monitor` |
| `HEALTH_STATE_CHANGED` | new state | `health_monitor` | `mqtt_service` (publish status) |

### Why Not ESP-IDF Event Loop?

ESP-IDF provides `esp_event_loop`. We use a custom lightweight event bus because:
1. Host-testable without ESP-IDF runtime
2. Simpler subscription model for our ~10 event types
3. No dynamic event base registration complexity
4. The custom bus already exists and works

If the event bus becomes a bottleneck, migrating to `esp_event_loop` is straightforward
because the interface is similar (publish/subscribe with type + optional data).

## 7. Data Flow

### Primary Path: Radio Frame → MQTT

```
CC1101 FIFO
    │ SPI read (radio_rx_task, Core 1)
    ▼
RawRadioFrame { bytes[290], len, rssi, lqi, crc_ok }
    │ xQueueSend(frame_queue)
    ▼
pipeline_task (Core 0)
    │
    ├─ WmbusPipeline::from_radio_frame()
    │   → WmbusFrame { raw_bytes (canonical), metadata { rssi, lqi, crc_ok, timestamp, frame_len } }
    │   → raw_hex is derived only for API/MQTT/UI payloads
    │
    ├─ TelegramRouter::route()
    │   ├─ DedupService::seen_recently(dedup_key(raw_bytes), timestamp)?
    │   │   → yes: RouteDecision::SUPPRESS_DUPLICATE
    │   │   → no:  DedupService::remember(dedup_key(raw_bytes), timestamp)
    │   │          RouteDecision::PUBLISH_RAW
    │   └─ !crc_ok? → additionally flag for event publish
    │
    ├─ if PUBLISH_RAW:
    │   → build MqttOutboxItem { topic=RAW_FRAME, payload=json }
    │   → xQueueSend(mqtt_outbox)
    │
    └─ update diagnostics counters
        (frames_received++, frames_published++ or frames_duplicate++)

mqtt_task (Core 0)
    │ xQueueReceive(mqtt_outbox)
    ▼
    esp_mqtt_client_publish(topic, payload, qos=0)
    │
    └─ update counters (mqtt_publishes++ or mqtt_failures++)
```

### Secondary Path: Periodic Telemetry

```
health_task (every 30s)
    │
    ├─ collect MetricsService::snapshot()
    ├─ collect HealthMonitor::snapshot()
    ├─ collect DiagnosticsService::snapshot()
    │
    ├─ build status payload JSON
    │   → MqttOutboxItem { topic=STATUS, payload=json }
    │   → xQueueSend(mqtt_outbox)
    │
    └─ build telemetry payload JSON
        → MqttOutboxItem { topic=TELEMETRY, payload=json }
        → xQueueSend(mqtt_outbox)
```

### Web Panel Path

```
Browser → HTTP GET/POST → httpd task
    │
    ├─ static files: served from SPIFFS (/web/*)
    │
    └─ API endpoints: /api/*
        │
        ├─ auth check (AuthService::validate_session)
        │   → 401 if invalid
        │
        └─ route to ApiHandler
            ├─ GET /api/bootstrap → startup mode/password-set hints for frontend routing
            ├─ GET /api/status → HealthMonitor + MetricsService snapshot
            ├─ GET /api/telegrams → recent frame buffer
            ├─ GET /api/diagnostics → DiagnosticsService snapshot
            ├─ GET /api/config → ConfigStore::config() (redacted)
            ├─ POST /api/config → validate + save
            ├─ POST /api/ota/upload → streamed binary upload (application/octet-stream)
            ├─ POST /api/ota/url → OtaManager::begin_url_ota
            ├─ GET /api/logs → PersistentLogBuffer::lines()
            ├─ GET /api/support-bundle → SupportBundleService::generate
            └─ POST /api/auth/login → AuthService::login
```

## 8. Configuration Strategy

### Config Model

```cpp
struct AppConfig {
    uint32_t version;           // Schema version for migration

    struct Device {
        char name[32];          // Human-readable device name
        char hostname[32];      // mDNS hostname
    } device;

    struct Wifi {
        char ssid[33];          // WiFi SSID
        char password[65];      // WiFi password (SECRET)
        uint8_t max_retries;    // Before entering AP fallback
    } wifi;

    struct Mqtt {
        bool enabled;
        char host[128];         // Broker hostname/IP
        uint16_t port;          // Default: 1883
        char username[64];      // MQTT username (SECRET)
        char password[64];      // MQTT password (SECRET)
        char prefix[64];        // Topic prefix, e.g. "wmbus-gw"
        char client_id[64];     // MQTT client ID
        uint8_t qos;            // Default: 0
        bool use_tls;           // Enable TLS
    } mqtt;

    struct Radio {
        uint32_t frequency_khz; // Default: 868950 (T-mode)
        uint8_t data_rate;      // T-mode default
        bool auto_recovery;     // Auto-recover from radio errors
    } radio;

    struct Logging {
        uint8_t level;          // ESP_LOG_* level
    } logging;

    struct Auth {
        char admin_password_hash[97]; // "salt_hex:hash_hex" SHA-256 (SECRET)
        uint32_t session_timeout_s;   // Session expiry, default 3600
    } auth;
};
```

### Persistence Strategy

- **NVS namespace:** `wg_config`
- **Storage format:** Serialized as a single NVS blob (simple, atomic writes)
- **Alternative considered:** Per-field NVS keys. Rejected because migration and atomic updates are harder.
- **Validation:** Always validate before saving. Invalid config is rejected, never persisted.
- **Migration:** On load, if `version < kCurrentConfigVersion`, run migration chain.
- **Import/Export:** JSON format via API. Secrets are redacted in export. Import validates before applying.
- **Factory Reset:** Writes default config and reboots.
- **Secret fields:** `wifi.password`, `mqtt.username`, `mqtt.password`, `auth.admin_password_hash` — these are redacted as `"***"` in any export, API response, log, or support bundle.

### Config Version Strategy

| Version | Changes |
|---------|---------|
| 1 | Initial schema |
| 2+ | Future: added fields get defaults; removed fields are dropped; renamed fields are mapped |

Migration functions are chained: `migrate_v1_to_v2()`, `migrate_v2_to_v3()`, etc.

## 9. MQTT Strategy

### Topic Hierarchy

All topics are prefixed with `{prefix}/{device_id}/` where:
- `prefix` defaults to `wmbus-gw` (configurable)
- `device_id` defaults to last 6 hex chars of MAC address

| Topic | QoS | Retain | Purpose |
|-------|-----|--------|---------|
| `{prefix}/{id}/status` | 0 | true | Online/offline status, firmware version |
| `{prefix}/{id}/telemetry` | 0 | false | Periodic metrics (heap, uptime, counters) |
| `{prefix}/{id}/events` | 0 | false | Discrete events (radio error, OTA, config change) |
| `{prefix}/{id}/rf/raw` | 0 | false | Raw received telegram with metadata |

### Last Will and Testament

- **Topic:** `{prefix}/{id}/status`
- **Payload:** `{"online": false}`
- **QoS:** 0, **Retain:** true

On successful connect, immediately publish: `{"online": true, "firmware": "x.y.z", "ip": "..."}`.

### Reconnect Strategy

Reconnect is handled by the ESP-IDF MQTT client library with a fixed reconnect timeout of 1000 ms. Custom exponential backoff is not implemented; the library handles retries internally.

On connect/disconnect, the MQTT event handler emits `MQTT_CONNECTED` / `MQTT_DISCONNECTED` events via the event bus.

### Payload Schemas

Defined in detail in `docs/MQTT_TOPICS.md`.

## 10. Web Panel Architecture

### Technology

- **No heavy JS framework.** Vanilla HTML + CSS + JS.
- **Single-page feel** with tab navigation (no router library).
- **Assets served from SPIFFS** partition mounted at `/storage`.
- **API calls** via `fetch()` to `/api/*` endpoints.
- **Authentication/startup:** UI first calls `/api/bootstrap` to decide between Initial Setup and Sign In. Sign In uses POST `/api/auth/login`; session token is stored in localStorage and sent as `Authorization: Bearer {token}`.

### Pages

| Page | API Endpoints Used | Purpose |
|------|-------------------|---------|
| Dashboard | `/api/status` | Health, uptime, key counters |
| Live Telegrams | `/api/telegrams` | Recent raw frames with metadata |
| RF Diagnostics | `/api/diagnostics/radio` | RSSI histogram, error counts, radio state |
| MQTT Status | `/api/diagnostics/mqtt` | Connection state, publish counts, errors |
| Configuration | `/api/config` | View/edit all settings (secrets redacted) |
| OTA | `/api/ota/*` | Upload firmware, trigger URL OTA, see status |
| System | `/api/system/*` | Reboot, factory reset, support bundle download |
| Logs | `/api/logs` | Recent log lines from persistent buffer |

### Design Principles

- **Functional over decorative.** Every element serves a diagnostic or operational purpose.
- **Responsive.** Works on mobile (field service from phone).
- **Minimal JS.** No build step, no npm, no bundler. Ship raw files.
- **Fast load.** Total assets under 50KB uncompressed.

## 11. OTA Strategy

### Partition Layout

The existing `partitions.csv` provides:
- `ota_0` (1.5MB) — active app slot (initial flash target)
- `ota_1` (1.5MB) — OTA update slot
- `otadata` (8KB) — tracks which OTA partition is active
- `storage` (896KB) — SPIFFS storage for web assets/support data

### OTA Flow

1. **Begin:** Validate source (upload or URL), check free partition
2. **Write:** Stream image data to inactive OTA partition
3. **Validate:** Check image header magic bytes, app descriptor
4. **Commit:** Set inactive partition as next boot partition via `esp_ota_set_boot_partition`
5. **Reboot:** Trigger restart
6. **Health Check:** After boot, new firmware has N seconds to call `esp_ota_mark_app_valid_cancel_rollback()`
7. **Rollback:** If health check fails (watchdog timeout, crash loop), ESP-IDF automatically rolls back

### Upload OTA

- HTTP POST binary upload to `/api/ota/upload`
- Content-Type: `application/octet-stream` (or `application/x-binary`)
- Current implementation streams chunks directly to OTA manager write path
- If packet is too large or OTA is already running, upload is rejected explicitly

### URL OTA

- HTTP POST with `{"url": "https://..."}` to `/api/ota/url`
- Firmware downloads using `esp_https_ota`
- TLS certificate validation (bundle or skip for testing)

### Safety Checks

- Image size must fit partition (< 1.5MB)
- Image must have valid ESP-IDF app descriptor
- OTA rejected if another OTA is in progress
- OTA during provisioning mode is not a primary operational path; use with caution

## 12. Diagnostics Strategy

### Counters (Maintained at All Times)

| Category | Counter | Type |
|----------|---------|------|
| Radio | `frames_received` | uint32_t, monotonic |
| Radio | `frames_crc_ok` | uint32_t |
| Radio | `frames_crc_fail` | uint32_t |
| Radio | `fifo_overflows` | uint32_t |
| Radio | `radio_resets` | uint32_t |
| Radio | `radio_recoveries` | uint32_t |
| Pipeline | `frames_published` | uint32_t |
| Pipeline | `frames_duplicate` | uint32_t |
| MQTT | `mqtt_publishes` | uint32_t |
| MQTT | `mqtt_publish_failures` | uint32_t |
| MQTT | `mqtt_reconnects` | uint32_t |
| WiFi | `wifi_reconnects` | uint32_t |
| System | `uptime_seconds` | uint32_t |
| System | `free_heap_bytes` | uint32_t |
| System | `min_free_heap_bytes` | uint32_t |
| System | `reset_reason` | enum |

### Health States

```
STARTING → HEALTHY → WARNING → ERROR
    ↑                    │         │
    └────────────────────┴─────────┘  (recovery)
```

Transitions are caller-driven (not automatic):
- `STARTING → HEALTHY`: `report_healthy()` called by `health_task` when WiFi + MQTT OK
- `→ WARNING`: `report_warning(msg)` called when a subsystem reports a problem
- `→ ERROR`: `report_error(msg)` called on critical failure
- `WARNING → HEALTHY`: `report_healthy()` clears warning state
- `ERROR` persists until `report_healthy()` is explicitly called

*Note: The current `health_task` in `app_core` drives these transitions based on WiFi/MQTT state. Automatic threshold-based transitions (e.g., "MQTT disconnected > 60s") are not yet implemented.*

### Support Bundle

JSON document containing:
- Diagnostics snapshot (radio, MQTT, WiFi counters and state)
- Metrics snapshot (uptime, heap)
- Health snapshot
- Config (with secrets redacted)
- Recent log lines from persistent buffer

*Not yet implemented: device identity block, last N events, reset reason history.*

## 13. Security Strategy

Detailed in `docs/SECURITY.md`. Summary:

### Authentication
- Admin password set during provisioning (or default that must be changed)
- Password stored as SHA-256 hash in NVS config
- Login returns session token (random 32-byte hex)
- Token expires after configurable timeout (default 1 hour)
- Single active session (new login invalidates previous)

### Endpoint Protection
- Management `/api/*` endpoints require valid session token; unauthenticated startup/auth endpoints are `/api/bootstrap` and `/api/auth/login`
- Static file serving (`/web/*`) is unauthenticated (HTML/JS/CSS are not sensitive)
- Auth check is in HTTP server middleware, not scattered across handlers

### Input Validation
- Config changes validated before persistence
- OTA image validated before commit
- MQTT topic/payload length bounds enforced
- HTTP request size limits enforced by ESP-IDF httpd config

### Secret Handling
- Passwords/tokens never appear in logs (ESP_LOG)
- Config export replaces secret fields with `"***"`
- Support bundle redacts secrets
- Web UI never receives plaintext passwords from API

### Safe Defaults
- Auth enabled by default
- First-boot provisioning has no fixed default password; backend login compatibility accepts any non-empty password until admin hash is set, while UI enforces explicit Initial Setup flow
- MQTT TLS disabled by default (most local brokers don't use it) but configurable
- Radio auto-recovery enabled by default

## 14. Testing Strategy

Detailed in `docs/TESTING.md`. Summary:

### Host Tests (CMake + CTest on build machine)

Tests that run on the development machine without ESP-IDF or hardware:

| Test File | Tests |
|-----------|-------|
| `test_config_validation.cpp` | Valid/invalid configs, boundary values, missing fields |
| `test_config_migration.cpp` | v0→v1 migration, unknown version handling |
| `test_dedup.cpp` | Seen/not-seen, window expiry, prune behavior |
| `test_mqtt_payloads.cpp` | JSON payload structure, field presence, escaping |
| `test_auth_helpers.cpp` | Password hash verification, token generation/validation |
| `test_health_logic.cpp` | State transitions, counter increments |
| `test_wmbus_pipeline.cpp` | Frame conversion, hex encoding, metadata extraction |

### What Cannot Be Host-Tested

| Area | Reason | Alternative |
|------|--------|------------|
| SPI/CC1101 communication | Hardware-dependent | Manual test with logic analyzer; HIL test rig |
| WiFi connection | ESP-IDF WiFi stack | Manual test on device |
| NVS persistence | ESP-IDF NVS API | Integration test on device |
| OTA partition writes | ESP-IDF OTA API | Manual test with binary upload |
| HTTP server | ESP-IDF httpd | `curl`-based integration test script |
| SPIFFS file serving | ESP-IDF VFS | Manual test on device |

### Static Analysis

- `.clang-format` enforces consistent style
- CI runs `clang-format --dry-run --Werror` on all source files
- Future: `clang-tidy` with ESP-IDF-appropriate checks

### CI Pipeline

1. **Build:** `idf.py build` (requires ESP-IDF Docker image or installed toolchain)
2. **Host Tests:** CMake build + CTest in `tests/host/`
3. **Format Check:** clang-format verification
4. **Future:** Static analysis, coverage reporting

## 15. Proposed Repository Tree

```
water-gateway/
├── CMakeLists.txt                  # Top-level ESP-IDF project
├── sdkconfig.defaults              # Default Kconfig values
├── partitions.csv                  # Flash partition table
├── PROJECT_RULES.md                # Mandatory project rules
├── PROJECT_BRIEF.md                # Project brief
├── README.md                       # Project overview
├── .clang-format                   # Code formatting rules
├── .editorconfig                   # Editor settings
├── .gitignore                      # Git ignores
│
├── main/
│   ├── CMakeLists.txt
│   └── app_main.cpp                # Entry point (thin, delegates to app_core)
│
├── components/
│   ├── common/                     # Shared types, errors, Result<T>
│   ├── event_bus/                  # Publish/subscribe event system
│   ├── config_store/               # Versioned NVS config
│   ├── storage_service/            # SPIFFS abstraction
│   ├── wifi_manager/               # WiFi STA lifecycle
│   ├── ntp_service/                # SNTP sync
│   ├── mdns_service/               # mDNS advertisement
│   ├── provisioning_manager/       # First-boot setup
│   ├── radio_cc1101/               # CC1101 SPI driver
│   ├── radio_state_machine/        # Radio lifecycle FSM
│   ├── wmbus_minimal_pipeline/     # Frame → metadata pipeline
│   ├── dedup_service/              # Duplicate detection
│   ├── telegram_router/            # Route decisions
│   ├── mqtt_service/               # MQTT client + topics + payloads
│   ├── auth_service/               # Authentication + sessions
│   ├── http_server/                # HTTP server wrapper
│   ├── api_handlers/               # REST API handlers
│   ├── web_ui/                     # Web UI component (README only)
│   ├── ota_manager/                # OTA lifecycle
│   ├── diagnostics_service/        # Diagnostic aggregation
│   ├── metrics_service/            # Runtime metrics
│   ├── health_monitor/             # Health state machine
│   ├── watchdog_service/           # Watchdog integration
│   ├── persistent_log_buffer/      # Log ring buffer
│   ├── support_bundle_service/     # Support bundle generation
│   └── app_core/                   # Boot orchestration
│
├── web/
│   ├── index.html                  # Main SPA page
│   ├── app.js                      # Application logic
│   └── styles.css                  # Styles
│
├── docs/
│   ├── ARCHITECTURE.md             # This document
│   ├── REPO_LAYOUT.md              # Repository structure
│   ├── CONFIGURATION.md            # Config model and usage
│   ├── MQTT_TOPICS.md              # MQTT topic/payload contracts
│   ├── WEB_UI.md                   # Web panel documentation
│   ├── OTA.md                      # OTA procedures
│   ├── SECURITY.md                 # Threat model and security design
│   ├── PROVISIONING.md             # First-boot setup
│   ├── TESTING.md                  # Test strategy
│   ├── OPERATIONS.md               # Operational guidance
│   ├── TROUBLESHOOTING.md          # Common issues
│   └── LIMITATIONS.md              # Known limitations
│
├── tests/
│   ├── README.md                   # Test overview
│   ├── host/
│   │   ├── CMakeLists.txt          # Host test build
│   │   ├── test_config_validation.cpp
│   │   ├── test_config_migration.cpp
│   │   ├── test_dedup.cpp
│   │   ├── test_mqtt_payloads.cpp
│   │   ├── test_auth_helpers.cpp
│   │   ├── test_health_logic.cpp
│   │   └── test_wmbus_pipeline.cpp
│   └── fixtures/
│       ├── README.md
│       └── sample_frames.json
│
└── .github/
    └── workflows/
        └── ci.yml                  # CI pipeline
```

## 16. Tradeoffs, Limitations, and Risks

### Accepted Tradeoffs

| Decision | Tradeoff | Rationale |
|----------|----------|-----------|
| Polling CC1101 instead of GDO interrupt | Higher CPU usage, slightly higher latency | Simpler to debug and implement correctly; interrupt mode is future optimization |
| Single NVS blob for config | Slower writes (full blob) vs per-field | Atomic updates, simpler migration, simpler validation |
| In-memory log buffer (not flash) | Logs lost on reboot | Flash wear concern; persistent logs are future optimization with flash ring buffer |
| Vanilla JS web panel | No component reuse, manual DOM manipulation | Zero build tooling, tiny asset size, no dependency risk |
| SHA-256 for password hash (not bcrypt) | Less brute-force resistant | ESP32 has no bcrypt library in IDF; SHA-256 with salt is adequate for local network device |
| Single active session | Only one admin logged in at a time | Simpler session management; adequate for service device |
| QoS 0 for MQTT by default | Possible message loss | Telegrams are continuous; missing one is acceptable; QoS 1/2 adds latency and complexity |

### Known Limitations

1. **No Wireless M-Bus decryption.** AES-128 decryption of meter data is out of scope (external decoder responsibility).
2. **T-mode only initially.** C-mode and S-mode support are future extensions.
3. **No HTTPS for web panel.** ESP32 HTTP server with TLS is resource-heavy; local network assumed.
4. **4MB flash constraint.** 1.5MB per OTA partition limits firmware size.
5. **Single CC1101.** No multi-radio or diversity reception.

### Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| CC1101 register config wrong for T-mode | Medium | High (no frames) | Use known-good register sets from open-source projects; test with real meter |
| WMBus frame format varies by manufacturer | Medium | Medium (parse failures) | Minimal parsing only; let external decoders handle complexity |
| ESP32 heap exhaustion under load | Low | High (crash) | Bounded queues, stack watermark monitoring, heap tracking |
| MQTT broker unreachable for extended time | Medium | Medium (queue overflow) | Bounded outbox, drop oldest on overflow, counter tracking |
| NVS corruption | Low | High (config loss) | Default config fallback, config export for backup |
| OTA bricking | Low | Critical | Rollback partition, boot health check, factory partition as ultimate fallback |
