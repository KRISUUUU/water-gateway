# MQTT Topics and Payload Contracts

## Topic Hierarchy

All topics follow: `{prefix}/{device_id}/{path}`

- **prefix**: Configurable, default `wmbus-gw`
- **device_id**: Derived from ESP32 MAC address (last 3 bytes, hex), e.g. `a1b2c3`

Example with defaults: `wmbus-gw/a1b2c3/status`

## Topics

### `{prefix}/{id}/status`

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
  "health": "healthy",
  "timestamp": "2025-01-15T12:00:00Z"
}
```

#### Payload: Offline (Last Will)

```json
{
  "online": false
}
```

### `{prefix}/{id}/telemetry`

Periodic system metrics for monitoring and alerting.

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** Every 30 seconds (configurable)

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
  "mqtt_publishes": 4420,
  "mqtt_failures": 2,
  "timestamp": "2025-01-15T12:00:00Z"
}
```

### `{prefix}/{id}/events`

Discrete system events for alerting and audit.

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** On event occurrence

#### Payload

```json
{
  "event": "radio_error",
  "severity": "warning",
  "message": "FIFO overflow detected, radio reset triggered",
  "timestamp": "2025-01-15T12:00:05Z"
}
```

#### Event Types

| `event` Value | Severity | Description |
|---------------|----------|-------------|
| `boot` | `info` | Device booted (includes reset reason) |
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

### `{prefix}/{id}/rf/raw`

Raw received WMBus telegram with RF metadata. This is the primary data output
for external decoders (wmbusmeters, Home Assistant, custom consumers).

- **QoS:** 0
- **Retain:** false
- **Publish frequency:** On each unique (non-duplicate) frame reception

#### Payload

```json
{
  "raw_hex": "2C4493157856341201078C2027900F002C25B30A000021...",
  "frame_length": 44,
  "rssi_dbm": -65,
  "lqi": 45,
  "crc_ok": true,
  "timestamp": "2025-01-15T12:00:01Z",
  "rx_count": 4524
}
```

#### Field Reference

| Field | Type | Description |
|-------|------|-------------|
| `raw_hex` | string | Complete received frame as uppercase hex string |
| `frame_length` | integer | Number of raw bytes |
| `rssi_dbm` | integer | Received signal strength in dBm |
| `lqi` | integer | Link Quality Indicator (CC1101-specific, 0-127) |
| `crc_ok` | boolean | Whether CC1101 hardware CRC check passed |
| `timestamp` | string | ISO 8601 UTC timestamp of reception |
| `rx_count` | integer | Monotonic frame reception counter (useful for detecting gaps) |

## Contract Stability

### Versioning Policy

- Fields in published payloads are **additive only**. Existing fields are never
  removed or renamed without a major version bump.
- New fields may be added to any payload at any time. Consumers must tolerate
  unknown fields.
- The `raw_hex` field in `rf/raw` is the primary contract. External decoders
  should depend only on this field being present and correct.

### Breaking Changes

If a breaking change to topic structure or payload format is necessary:

1. Document the change in this file and in release notes
2. Bump the firmware major version
3. Add a `payload_version` field to affected topics during transition
4. Maintain backward compatibility for at least one major version where practical

## Home Assistant Integration

### Auto-Discovery (Future)

MQTT auto-discovery for Home Assistant is planned as a future enhancement.
For now, manual YAML configuration is required.

### Manual Configuration Example

```yaml
mqtt:
  sensor:
    - name: "WMBus Gateway Status"
      state_topic: "wmbus-gw/a1b2c3/status"
      value_template: "{{ 'Online' if value_json.online else 'Offline' }}"
      json_attributes_topic: "wmbus-gw/a1b2c3/status"
      availability:
        - topic: "wmbus-gw/a1b2c3/status"
          value_template: "{{ 'online' if value_json.online else 'offline' }}"

    - name: "WMBus Gateway Uptime"
      state_topic: "wmbus-gw/a1b2c3/telemetry"
      value_template: "{{ value_json.uptime_s }}"
      unit_of_measurement: "s"
      device_class: "duration"

    - name: "WMBus Frames Received"
      state_topic: "wmbus-gw/a1b2c3/telemetry"
      value_template: "{{ value_json.frames_received }}"
      state_class: "total_increasing"
```

### wmbusmeters Integration

The `rf/raw` topic provides the raw hex telegram that wmbusmeters can process.
Configure wmbusmeters to subscribe to `wmbus-gw/+/rf/raw` and parse the
`raw_hex` field.

## Topic Summary Table

| Topic | Direction | QoS | Retain | Frequency |
|-------|-----------|-----|--------|-----------|
| `{p}/{id}/status` | Gateway → Broker | 0 | true | On connect / state change |
| `{p}/{id}/telemetry` | Gateway → Broker | 0 | false | Every 30s |
| `{p}/{id}/events` | Gateway → Broker | 0 | false | On event |
| `{p}/{id}/rf/raw` | Gateway → Broker | 0 | false | Per unique frame |
