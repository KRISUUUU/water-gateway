# PROJECT_BRIEF.md

## Goal
Build a production-minded ESP-IDF firmware for ESP32 + CC1101 as a Wireless M-Bus 868 MHz receiver/gateway for water meter style telegrams.

## Current Phase
The project is now in a combined Phase D + E.

### Phase D — User Layer / Product Layer
The project should become a genuinely useful gateway product:
- detected meters
- watchlist
- aliases
- filtering
- live telegram UX
- better diagnostics UX
- better MQTT / Home Assistant product flow

### Phase E — Hardening / Premium Readiness
The project should become more robust and release-worthy:
- OTA hardening
- rollback sanity
- auth hardening
- long-run stability improvements
- warning cleanup
- size awareness
- documentation / release polish

## Architecture
Variant B:
- ESP32 + CC1101 receives raw radio frames and publishes raw telegrams + metadata + telemetry.
- Heavy meter-specific decoding remains external by default.
- Firmware focuses on RF reception, transport, diagnostics, OTA, configuration, and serviceability.

## Current Reality
The repository:
- builds successfully
- boots successfully on ESP32
- provisioning AP and base web flow work
- static assets are packaged and served
- hardware RF validation is still incomplete
- product usability can now be expanded significantly
- hardening work is now the right next step

## Non-goals Right Now
Do NOT prioritize:
- giant meter-specific decoder logic
- overengineered frontend
- fake RF claims
- broad unrelated feature creep

## Success Criteria For This Phase
This phase is successful when:
- detected meters and watchlist exist coherently
- live telegrams UX is useful
- diagnostics UX is useful
- MQTT/HA flow is more usable
- OTA/auth/security are more robust
- long-run operability is improved
- docs are polished and truthful
- the repo is ready for a later premium finish once hardware validation catches up

## What Comes After This Phase
After this phase and after more real hardware validation, the project can move into final premium/release stage:
- deeper RF tuning
- final HA polish
- richer product UX
- final OTA upload verification
- extended HIL/stability validation
- release packaging/versioning