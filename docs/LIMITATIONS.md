# Known Limitations

## Radio / RF

1. **T-mode only.** Firmware targets Wireless M-Bus T-mode on 868.95 MHz. C-mode and S-mode are not implemented.
2. **No application-layer decryption.** AES-128 decryption of encrypted meter payloads is not performed. Accepted link-layer frames are published as hex for external decoders (for example wmbusmeters).
3. **Single CC1101.** Only one radio is supported; no diversity or multi-radio aggregation.
4. **Polling-based RX.** The radio task polls the CC1101 over SPI rather than GDO interrupts. Under very high airtime, FIFO pressure can still occur; see `frames_incomplete` / `frames_dropped_too_long` / `fifo_overflows`.
5. **No frequency hopping.** The radio uses a single configured channel (default 868.950 MHz from the register set).
6. **RX FIFO length and pipeline contract.** The CC1101 driver reads a length-prefixed burst from the FIFO and appends two status bytes. The **W-MBus pipeline** (`WmbusPipeline::from_radio_frame`) expects `RawRadioFrame.length` to be **even** and treats **every** byte pair as two **3-of-6** symbols (six valid bits each, masked to `0x3F`) that decode to one link-layer octet. If the on-air PHY delivers a different packing (for example already-decoded link bytes without 3-of-6 expansion), `from_radio_frame` will fail validation and **no** `WmbusFrame` is produced. End-to-end reception requires the RF path to match this contract; resolve mismatches in the driver or PHY configuration, not by guessing in documentation.
7. **CC1101 vs DLL CRC.** `RawRadioFrame::crc_ok` reflects the CC1101 **hardware** status bit (append status in RX FIFO). Application acceptance uses **EN 13757-4 DLL CRC** after 3-of-6 decode (`metadata.crc_ok` is set only when the pipeline succeeds). The two are independent.
8. **Hardware sync.** The register set programs `SYNC1`/`SYNC0` (`0x54`/`0x3D`) to reduce false packet sync on random noise; this does not remove the need for correct demodulation and pipeline decoding.
9. **Detected meter identity is best-effort.** Identity uses manufacturer, device serial, and device type from the clean decoded link layer when present; otherwise a signature prefix. It is not full vendor-specific application decoding.

## Networking

1. **2.4 GHz WiFi only.** ESP32 does not support 5 GHz.
2. **No Ethernet.** Only WiFi (STA or provisioning AP) is used for IP connectivity.
3. **HTTP only for web panel.** TLS on the embedded HTTP server is not enabled; use on a trusted LAN or behind a reverse proxy.
4. **IPv4.** IPv6 is not a target configuration.

## MQTT

1. **QoS 0 by default.** Messages may be lost if the broker is unavailable; higher QoS increases resource use.
2. **MQTT v3.1.1.** The ESP-IDF client does not use MQTT v5.
3. **Volatile outbox.** MQTT queue items live in RAM; reboot clears unsent publishes. One item is held in a carry-over buffer and retried on reconnect, but extended outages that overflow the 32-slot queue still cause drops (logged as warnings).

## Configuration

1. **Single NVS blob.** The whole `AppConfig` is stored as one key; updates rewrite the blob (atomic at NVS level).
2. **No flash encryption by default.** Physical flash read exposes NVS unless NVS encryption is enabled in the product flow.
3. **Single admin role.** No RBAC or read-only users.

## OTA

1. **~1.5 MB image limit** per OTA slot with the current partition table.
2. **No firmware signing** in the default project (Secure Boot is a product choice).
3. **Full image only.** No delta OTA.

## Web Panel

1. **No WebSocket.** Data refresh uses polling (`fetch` on timers).
2. **No offline PWA.** The browser must reach the device IP or hostname.
3. **Vanilla JS.** No SPA framework; larger UI changes are manual.

## General

1. **RAM log buffer.** `persistent_log_buffer` is in-memory; reset clears logs.
2. **32-bit counters.** Diagnostic counters wrap; monitoring tools should tolerate wrap.
3. **Clock drift.** Between NTP syncs, time is based on the ESP32 clock source.
