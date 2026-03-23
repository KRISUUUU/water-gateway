# Water Gateway

Production-minded ESP-IDF firmware for ESP32 + CC1101 acting as a Wireless M-Bus 868 MHz RF gateway for water meter style telegrams.

## Project Goal

This project implements a robust ESP32 + CC1101 firmware focused on:

- receiving Wireless M-Bus telegrams on 868 MHz
- collecting raw frames and metadata
- publishing raw telegrams and gateway telemetry outward
- providing a built-in web panel for service and diagnostics
- supporting OTA updates with rollback thinking
- supporting first-boot provisioning
- integrating with MQTT / Home Assistant / external decoders such as wmbusmeters

## Architectural Direction

This project follows **Variant B**:

- ESP32 + CC1101 is primarily a robust RF receiver and gateway
- heavy meter-specific decoding remains external by default
- the firmware focuses on RF, transport, diagnostics, configuration, OTA, and operational visibility

## Current Status

Repository scaffold / architecture phase.

See:
- `PROJECT_RULES.md`
- `PROJECT_BRIEF.md`
- `docs/ARCHITECTURE.md`
- `docs/REPO_LAYOUT.md`

## Repository Structure

- `main/` - application entrypoint
- `components/` - firmware components
- `docs/` - architecture, operational, and implementation documentation
- `tests/` - host-side tests and fixtures
- `web/` - static assets for the embedded web UI

## Build Notes

This project is intended to use ESP-IDF.

Build and setup instructions will be expanded as implementation progresses.

## Quality Goals

- modular architecture
- explicit interfaces
- resilience and recoverability
- stable MQTT contracts
- serviceable web UI
- OTA + rollback
- clear docs
- host-testable logic where practical
