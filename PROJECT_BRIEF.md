# PROJECT_BRIEF.md

## Goal
Build a production-minded ESP-IDF firmware for ESP32 + CC1101 as a Wireless M-Bus 868 MHz receiver/gateway for water meter style telegrams.

## Current Project Phase
This repository is no longer an empty scaffold.
It is now in a **stabilization and pre-hardware-validation** phase.

That means the current priorities are:
- make the repository buildable
- make API/contracts internally consistent
- reduce architecture drift
- improve safety and correctness
- align tests and CI with reality
- prepare for first real hardware bring-up

## Architecture
Variant B:
- ESP32 + CC1101 receives radio frames and publishes raw telegrams + metadata + telemetry.
- Heavy meter-specific decoding remains external by default.
- Firmware focuses on RF reception, transport, diagnostics, OTA, configuration, and serviceability.

## Core Features (Target)
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

## Current Reality
Parts of the repository are already implemented, but not all parts are fully verified.
The codebase should currently be treated as:
- promising
- partially integrated
- requiring build verification
- requiring hardware validation
- requiring architecture cleanup in some areas

## Non-goals Right Now
At this stage, do NOT prioritize:
- new product UX features
- “detected meters” UX
- watchlist UX
- Home Assistant polish
- large web UI expansion
- feature growth for its own sake

Those can come later, after stabilization and hardware bring-up.

## Quality Expectations
- modular design
- clear contracts
- strong reliability thinking
- secure defaults
- testable non-hardware logic
- honest docs
- maintainability over years

## Success for This Phase
The current phase is successful when:
- host tests pass
- ESP-IDF build passes
- major API mismatches are gone
- `app_core` is cleaner
- docs match implementation
- OTA/auth/config flows are honest and safe
- board bring-up steps are clear
- the repo is ready for first real CC1101 + ESP32 validation