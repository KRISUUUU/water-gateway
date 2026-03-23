# Configuration

## Overview

All device configuration is stored in NVS (Non-Volatile Storage) as a single
versioned blob. Configuration changes are validated before persistence and
trigger a `CONFIG_CHANGED` event on the event bus.

## Config Model

The `AppConfig` struct in `config_store/config_models.hpp` defines all fields:

| Section | Fields | Notes |
|---------|--------|-------|
| `device` | `name`, `hostname` | Human-readable name and mDNS hostname |
| `wifi` | `ssid`, `password`, `max_retries` | WiFi STA credentials |
| `mqtt` | `enabled`, `host`, `port`, `username`, `password`, `prefix`, `client_id`, `qos`, `use_tls` | MQTT broker settings |
| `radio` | `frequency_khz`, `data_rate`, `auto_recovery` | CC1101 radio parameters |
| `logging` | `level` | ESP-IDF log level |
| `auth` | `admin_password_hash`, `session_timeout_s` | Panel authentication |

## NVS Storage

- **Namespace:** `wg_config`
- **Key:** `config`
- **Format:** Binary blob (raw struct bytes)
- **Atomic:** Writes are all-or-nothing at the NVS level

## Validation

Every config change passes through `validate_config()` which checks:
- Non-empty device name and hostname
- Valid hostname characters (alphanumeric + hyphens)
- If MQTT enabled: non-empty host, valid port (1-65535), non-empty prefix
- Radio frequency within 868-870 MHz range
- Session timeout between 60s and 86400s (1 minute to 24 hours)

Invalid config is rejected with a list of `ValidationIssue` entries indicating
which fields failed and why.

## Migration

On load, if the stored config version is older than the current firmware expects,
the migration chain runs:
1. `migrate_v0_to_v1()` — unversioned → v1 (apply all defaults)
2. Future: `migrate_v1_to_v2()`, etc.

If the stored version is newer than the firmware knows, the config is rejected
and defaults are used (prevents forward-incompatible corruption).

## Import / Export

- **Export:** `GET /api/config` returns JSON with secret fields replaced by `"***"`
- **Import:** `POST /api/config` accepts JSON, validates, and persists
- Import does not accept `"***"` for secrets — if a secret field is `"***"`, the
  existing value is preserved (allows non-secret-only updates)

## Factory Reset

`POST /api/system/factory-reset` writes default config and triggers reboot.
The device enters provisioning mode on next boot (no configured WiFi).

## Defaults

| Field | Default Value |
|-------|--------------|
| `device.name` | `"WMBus Gateway"` |
| `device.hostname` | `"wmbus-gw"` |
| `wifi.ssid` | `""` (unconfigured) |
| `wifi.password` | `""` |
| `wifi.max_retries` | `10` |
| `mqtt.enabled` | `true` |
| `mqtt.host` | `""` (must be configured) |
| `mqtt.port` | `1883` |
| `mqtt.prefix` | `"wmbus-gw"` |
| `mqtt.qos` | `0` |
| `mqtt.use_tls` | `false` |
| `radio.frequency_khz` | `868950` |
| `radio.auto_recovery` | `true` |
| `logging.level` | `ESP_LOG_INFO` (3) |
| `auth.session_timeout_s` | `3600` |
