# Configuration

## Overview

All device configuration is stored in NVS (Non-Volatile Storage) as a single
versioned blob. Changes are validated before persistence and emit `CONFIG_CHANGED`
on the event bus when saved.

## Config Model

The `AppConfig` struct in `config_store/config_models.hpp` defines all fields:

| Section | Fields | Notes |
|---------|--------|-------|
| `device` | `name`, `hostname` | Human-readable name and **MQTT topic segment** (second path component after `mqtt.prefix`) |
| `wifi` | `ssid`, `password`, `max_retries` | WiFi STA credentials |
| `mqtt` | `enabled`, `host`, `port`, `username`, `password`, `prefix`, `client_id`, `qos`, `use_tls` | MQTT broker settings |
| `radio` | `frequency_khz`, `data_rate`, `auto_recovery` | Radio-related settings (frequency reserved for future profile selection) |
| `logging` | `level` | ESP-IDF log level |
| `auth` | `admin_password_hash`, `session_timeout_s` | Panel authentication; hash buffer is 98 bytes including null terminator in struct layout |

## NVS Storage

- **Namespace:** `wg_config`
- **Key:** `config`
- **Format:** Binary blob (serialized `AppConfig`)
- **Concurrency:** Access is protected by a FreeRTOS mutex inside `ConfigStore`

## Validation

`validate_config()` enforces:

- Non-empty device name and hostname
- Valid hostname characters
- If MQTT enabled: non-empty host, valid port (1–65535), non-empty prefix
- Radio frequency within the 868–870 MHz band (as implemented in validation)
- Session timeout between 60 s and 86400 s

Invalid configs return structured `ValidationIssue` entries; they are not written to NVS.

## Migration

- Current schema version: `kCurrentConfigVersion` in `config_models.hpp`
- Older blobs are migrated (e.g. v0 → v1)
- Unknown **newer** version: load fails safely; defaults apply

## Import / Export

- **GET `/api/config`:** JSON with secrets as `"***"`
- **POST `/api/config`:** JSON import; `"***"` preserves existing secret

## Factory Reset

`POST /api/system/factory-reset` resets configuration and reboots.

## Defaults

| Field | Default Value |
|-------|----------------|
| `device.name` | `"WMBus Gateway"` |
| `device.hostname` | `"wmbus-gw"` |
| `wifi.ssid` | `""` (unconfigured) |
| `wifi.password` | `""` |
| `wifi.max_retries` | `10` |
| `mqtt.enabled` | `true` |
| `mqtt.host` | `""` (must be set if MQTT is used) |
| `mqtt.port` | `1883` |
| `mqtt.prefix` | `"wmbus-gw"` |
| `mqtt.qos` | `0` |
| `mqtt.use_tls` | `false` |
| `radio.frequency_khz` | `868950` |
| `radio.auto_recovery` | `true` |
| `logging.level` | `3` (INFO) |
| `auth.session_timeout_s` | `3600` |

## MQTT Topic Layout

Published topics are `{mqtt.prefix}/{device.hostname}/…`. With defaults this is
`wmbus-gw/wmbus-gw/…`. Set a distinct `device.hostname` per gateway when multiple
devices share one broker.
