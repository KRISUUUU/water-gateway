# MQTT Topics and Payload Contracts

## Topic Hierarchy

All topics follow: `{prefix}/{device_slug}/{path}`

- **prefix**: `AppConfig.mqtt.prefix`, default `wmbus-gw`
- **device_slug**: `AppConfig.device.hostname` (default `wmbus-gw`) — this is the **configured hostname**, not the Wi‑Fi MAC. Topic builders take `(prefix, hostname)` in `mqtt_topics.cpp`; `runtime_tasks.cpp` passes `cfg.device.hostname`.

Example: with default prefix and hostname: `wmbus-gw/wmbus-gw/status` (operators often change hostname to keep segments unique on a broker).

## Topics

### `{prefix}/{hostname}/status`

Device online/offline status. Used for Home Assistant availability.

- **QoS:** 0
- **Retain:** true
- **Publish frequency:** On connect, on significant state change
- **Last Will:** `{"online": false}` (set on MQTT connect)

#### Payload: Online

```json
{
  "online": true,
  "firmware_version": "1.0.0",
  "ip_address": "192.168.1.100",
  "hostname": "wmbus-gw",
  "uptime_s": 86400,
  "health": "healthy"
}
```

#### Payload: Offline (Last Will)

```json
{
  "online": false
}
```

### `{prefix}/{hostname}/telemetry`

Periodic system metrics for monitoring and alerting.

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** About every 30 seconds when MQTT is connected (see `health_task` in `app_core`)

#### Payload

```json
{
  "uptime_s": 86400,
  "free_heap_bytes": 120000,
  "min_free_heap_bytes": 95000,
  "wifi_rssi_dbm": -55,
  "mqtt_state": "connected",
  "radio_state": "rx_active",
  "frames_received": 4523,
  "frames_published": 4400,
  "frames_duplicate": 120,
  "frames_crc_fail": 3,
  "frames_dropped_queue_full": 9,
  "mqtt_publishes": 4420,
  "mqtt_failures": 2,
  "timestamp": "2025-01-15T12:00:00Z"
}
```

`frames_crc_fail` and `frames_dropped_queue_full` come from **radio** counters. The queue-full counter reports frames dropped before the W-MBus pipeline could process them.

### `{prefix}/{hostname}/events`

Discrete system events for alerting and audit.

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** When the firmware publishes an event (e.g. radio warning from router)

#### Payload

```json
{
  "event": "radio_event",
  "severity": "warning",
  "message": "Received frame with CRC failure",
  "timestamp": "2025-01-15T12:00:05Z"
}
```

#### Event Types (intended taxonomy)

| `event` Value | Severity | Description |
|---------------|----------|-------------|
| `boot` | `info` | Device booted |
| `wifi_connected` | `info` | WiFi connection established |
| `wifi_disconnected` | `warning` | WiFi connection lost |
| `mqtt_connected` | `info` | MQTT broker connection established |
| `mqtt_disconnected` | `warning` | MQTT broker connection lost |
| `radio_error` | `warning` | Radio hardware error (FIFO overflow, etc.) |
| `radio_recovered` | `info` | Radio recovered from error |
| `ota_started` | `info` | OTA update initiated |
| `ota_success` | `info` | OTA update completed successfully |
| `ota_failed` | `error` | OTA update failed |
| `config_changed` | `info` | Configuration was modified |
| `health_degraded` | `warning` | Health state moved to warning or error |
| `health_recovered` | `info` | Health state recovered to healthy |

Actual emitted `event` strings depend on `mqtt_service::payload_event` call sites.

### `{prefix}/{hostname}/rf/raw`

Decoded Wireless M-Bus **link-layer** octets (uppercase hex) plus RF metadata. This is the primary output for external decoders (wmbusmeters, Home Assistant, custom consumers).

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** On each **routed** non-duplicate publish (see `telegram_router`)

Only frames that pass `WmbusPipeline::from_radio_frame` (3-of-6 + DLL CRC + L-field length) are published here today.

#### Payload

```json
{
  "raw_hex": "09449315785634120107",
  "frame_length": 10,
  "rssi_dbm": -65,
  "lqi": 45,
  "crc_ok": true,
  "manufacturer_id": 5523,
  "device_id": 2018915346,
  "device_type": 7,
  "meter_key": "mfg:1593-id:78563412-t:07",
  "timestamp": "2025-01-15T12:00:01Z",
  "rx_count": 4524
}
```

#### Field Reference

| Field | Type | Description |
|-------|------|-------------|
| `raw_hex` | string | Uppercase hex of **clean decoded link-layer bytes** (L-field first, DLL block CRC bytes stripped), not raw 3-of-6 symbols |
| `frame_length` | integer | Count of clean decoded link-layer bytes in `raw_hex` |
| `rssi_dbm` | integer | RSSI in dBm (from CC1101 conversion) |
| `lqi` | integer | Link quality (CC1101 status byte, 0–127) |
| `crc_ok` | boolean | **DLL verification succeeded** in `WmbusPipeline` (`true` for published frames in the current code path) |
| `manufacturer_id` | integer | Parsed from decoded frame (little-endian field) |
| `device_id` | integer | Parsed from decoded frame |
| `device_type` | integer | Parsed device type byte from the clean link layer |
| `meter_key` | string | `identity_key()` from `WmbusFrame`, formatted as `mfg:<id>-id:<serial>-t:<device_type>` when parsed fields are present |
| `timestamp` | string | ISO 8601 UTC reception time (NTP-backed when available) |
| `rx_count` | integer | Monotonic counter from pipeline task |

## Contract Stability

### Versioning Policy

- Published fields are **additive** when possible.
- Consumers should tolerate unknown JSON keys.
- `raw_hex` is the primary payload for external decoding.

### Breaking Changes

If a breaking change is required:

1. Document in this file and release notes
2. Bump firmware major version
3. Consider a `payload_version` field during transition

## Home Assistant Integration

### Auto-Discovery (Future)

Planned enhancement; manual YAML is typical today.

### Manual Configuration Example

```yaml
mqtt:
  sensor:
    - name: "WMBus Gateway Status"
      state_topic: "wmbus-gw/<YOUR_HOSTNAME>/status"
      value_template: "{{ 'Online' if value_json.online else 'Offline' }}"
      json_attributes_topic: "wmbus-gw/<YOUR_HOSTNAME>/status"
      availability:
        - topic: "wmbus-gw/<YOUR_HOSTNAME>/status"
          value_template: "{{ 'online' if value_json.online else 'offline' }}"

    - name: "WMBus Gateway Uptime"
      state_topic: "wmbus-gw/<YOUR_HOSTNAME>/telemetry"
      value_template: "{{ value_json.uptime_s }}"
      unit_of_measurement: "s"
      device_class: "duration"

    - name: "WMBus Frames Received"
      state_topic: "wmbus-gw/<YOUR_HOSTNAME>/telemetry"
      value_template: "{{ value_json.frames_received }}"
      state_class: "total_increasing"
```

Replace `<YOUR_HOSTNAME>` with the device’s configured `device.hostname` (default `wmbus-gw`).

### wmbusmeters Integration

Subscribe to `wmbus-gw/+/rf/raw` (or your prefix) and pass `raw_hex` into your decoder pipeline.
`meter_key` helps correlate rows with the gateway’s Detected Meters / watchlist.

## Topic Summary Table

| Topic | Direction | QoS | Retain | Frequency |
|-------|-----------|-----|--------|-----------|
| `{p}/{hostname}/status` | Gateway → Broker | 0 | true | On connect / change |
| `{p}/{hostname}/telemetry` | Gateway → Broker | 0 | false | ~30s |
| `{p}/{hostname}/events` | Gateway → Broker | 0 | false | On event |
| `{p}/{hostname}/rf/raw` | Gateway → Broker | 0 | false | Per routed frame |
