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

## Networking

6. **2.4 GHz WiFi only.** ESP32 does not support 5 GHz WiFi.
7. **No Ethernet.** Only WiFi STA mode is supported for network connectivity.
8. **HTTP only (no HTTPS) for web panel.** TLS for the web panel is not
   enabled due to ESP32 memory constraints and certificate provisioning complexity.
9. **IPv4 only.** IPv6 support is not tested or configured.

## MQTT

10. **QoS 0 by default.** Messages may be lost during broker disconnect.
    QoS 1 is supported but increases latency and memory usage.
11. **No MQTT v5.** The ESP-IDF MQTT client uses MQTT v3.1.1.
12. **No persistent outbox.** If the device reboots while MQTT is disconnected,
    queued messages are lost.

## Configuration

13. **Single NVS blob.** The entire config is stored as one NVS blob. Partial
    updates rewrite the whole blob. This is atomic but slower than per-field storage.
14. **No config encryption by default.** NVS contents are readable if flash is
    extracted. NVS encryption is available but requires key management during
    manufacturing.
15. **No multi-user / RBAC.** There is a single admin account. No read-only
    user role exists.

## OTA

16. **1.5 MB OTA firmware size limit.** Constrained by current 4 MB partition layout.
17. **No firmware signing.** Secure Boot v2 is available in ESP-IDF but not
    enabled by default due to key management requirements.
18. **No delta/incremental OTA.** Full firmware image is required for each update.

## Web Panel

19. **No WebSocket.** Data updates use polling (fetch-based), not push.
20. **No offline capability.** The panel requires network access to the device.
21. **Vanilla JS only.** No component framework; complex UI extensions may
    require significant refactoring.

## General

22. **No persistent logs.** Log buffer is RAM-only and lost on reboot.
    Flash-based persistent logging is a future enhancement.
23. **32-bit counters.** Diagnostic counters are uint32_t and will wrap after
    ~4 billion events. Wrap is handled gracefully but may confuse monitoring
    if not accounted for.
24. **Clock accuracy.** NTP sync is periodic; between syncs, the ESP32 RTC
    crystal provides ~10 ppm accuracy (drift of ~0.86s/day).
