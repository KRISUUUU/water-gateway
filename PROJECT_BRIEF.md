# PROJECT_BRIEF.md

## Goal
Build a production-minded ESP-IDF firmware for ESP32 + CC1101 as a Wireless M-Bus 868 MHz receiver/gateway for water meter style telegrams.

## Current Phase
The project is now in a combined Phase B + C:

### Phase B — Working Technical Product
The firmware should become a coherent technical gateway product:
- first boot provisioning works
- config persistence works
- normal mode startup works
- Wi-Fi/MQTT/HTTP/auth flows are coherent
- diagnostics and OTA status are meaningful
- build/test/docs stay aligned

### Phase C — Usable Service/Web Product
The firmware should become usable through its built-in web panel:
- static web assets load correctly
- provisioning page works
- dashboard/status page works
- configuration page works
- diagnostics page works
- logs page works
- OTA page works
- the UX is simple, honest, and service-oriented

## Architecture
Variant B:
- ESP32 + CC1101 receives raw radio frames and publishes raw telegrams + metadata + telemetry.
- Heavy meter-specific decoding remains external by default.
- Firmware focuses on RF reception, transport, diagnostics, OTA, configuration, and serviceability.

## Current Reality
The repository:
- builds successfully
- boots successfully on ESP32
- provisioning AP starts
- base HTTP server works
- hardware RF validation is still incomplete
- product/service usability is not yet fully complete

## Non-goals Right Now
Do NOT prioritize yet:
- detected meters UI
- watchlist UI
- advanced Home Assistant integration polish
- full premium UX
- large feature expansion

## Success Criteria For This Phase
This phase is successful when:
- provisioning works end-to-end
- normal mode works end-to-end
- web assets are served correctly
- web panel is usable and honest
- diagnostics are meaningful
- config/auth/OTA flows are coherent
- docs match implementation
- the repo is ready for the next step once hardware is available again

## What Comes After This Phase
Later, after hardware validation and core usability are done, the project can move into a premium phase:
- detected meters
- watchlist
- HA polish
- richer UX
- OTA upload completion
- deeper diagnostics and product refinement