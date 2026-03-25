# Known Limitations

## Radio / RF

1. **T-mode only.** Initial firmware supports Wireless M-Bus T-mode (transmit mode
   from meters) on 868.95 MHz. C-mode and S-mode are not implemented.
2. **No decryption.** AES-128 decryption of encrypted meter telegrams is not
   performed. Encrypted frames are published raw for external decoder processing.
3. **Single CC1101.** Only one CC1101 radio is supported. No diversity reception.
4. **Polling-based RX.** The radio task polls CC1101 via SPI rather than using
   GDO interrupt pins. This may cause occasional frame loss under very high
   traffic (> ~50 frames/second). Interrupt-driven mode is a future enhancement.
5. **No frequency hopping.** The radio stays on a single configured frequency.
6. **Detected meter identity is best-effort.** Gateway identity uses observed
   manufacturer/device fields when available, otherwise falls back to a frame
   signature prefix. It is intentionally not full vendor-specific decoding.

## Networking

7. **2.4 GHz WiFi only.** ESP32 does not support 5 GHz WiFi.
8. **No Ethernet.** Only WiFi STA mode is supported for network connectivity.
9. **HTTP only (no HTTPS) for web panel.** TLS for the web panel is not
   enabled due to ESP32 memory constraints and certificate provisioning complexity.
10. **IPv4 only.** IPv6 support is not tested or configured.

## MQTT

11. **QoS 0 by default.** Messages may be lost during broker disconnect.
    QoS 1 is supported but increases latency and memory usage.
12. **No MQTT v5.** The ESP-IDF MQTT client uses MQTT v3.1.1.
13. **No persistent outbox.** If the device reboots while MQTT is disconnected,
    queued messages are lost.

## Configuration

14. **Single NVS blob.** The entire config is stored as one NVS blob. Partial
    updates rewrite the whole blob. This is atomic but slower than per-field storage.
15. **No config encryption by default.** NVS contents are readable if flash is
    extracted. NVS encryption is available but requires key management during
    manufacturing.
16. **No multi-user / RBAC.** There is a single admin account. No read-only
    user role exists.

## OTA

17. **1.5 MB OTA firmware size limit.** Constrained by current 4 MB partition layout.
18. **No firmware signing.** Secure Boot v2 is available in ESP-IDF but not
    enabled by default due to key management requirements.
19. **No delta/incremental OTA.** Full firmware image is required for each update.

## Web Panel

20. **No WebSocket.** Data updates use polling (fetch-based), not push.
21. **No offline capability.** The panel requires network access to the device.
22. **Vanilla JS only.** No component framework; complex UI extensions may
    require significant refactoring.

## General

23. **No persistent logs.** Log buffer is RAM-only and lost on reboot.
    Flash-based persistent logging is a future enhancement.
24. **32-bit counters.** Diagnostic counters are uint32_t and will wrap after
    ~4 billion events. Wrap is handled gracefully but may confuse monitoring
    if not accounted for.
25. **Clock accuracy.** NTP sync is periodic; between syncs, the ESP32 RTC
    crystal provides ~10 ppm accuracy (drift of ~0.86s/day).
