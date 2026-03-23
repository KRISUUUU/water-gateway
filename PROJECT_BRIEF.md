# PROJECT_BRIEF.md

## Goal
Build a production-minded ESP-IDF firmware for ESP32 + CC1101 as a Wireless M-Bus 868 MHz receiver/gateway for water meter style telegrams.

## Architecture
Variant B:
- ESP32 + CC1101 receives radio frames and publishes raw telegrams + metadata + telemetry.
- Heavy meter-specific decoding remains external by default.
- Firmware focuses on RF reception, transport, diagnostics, OTA, configuration, and serviceability.

## Core Features
- Wireless M-Bus raw frame reception
- RF metadata collection
- MQTT publishing
- built-in web panel
- OTA upload + HTTPS OTA + rollback
- first-boot provisioning
- auth-protected service UI
- diagnostics and metrics
- support bundle export
- clean integration with HA / MQTT / external decoders

## Non-goals
- giant vendor-specific full decoder
- throwaway prototype
- ESPHome-based implementation
- monolithic architecture

## Quality Expectations
- modular design
- clear contracts
- strong reliability thinking
- secure defaults
- testable non-hardware logic
- good operational documentation
- maintainable over years
