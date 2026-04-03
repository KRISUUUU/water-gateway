# AGENTS.md

This repository contains an ESP-IDF firmware project for a CC1101-based Wireless M-Bus gateway.
All changes must preserve buildability, runtime safety, and clear ownership boundaries between
radio, protocol, diagnostics, and transport layers.

## Working Rules

- Always inspect this `AGENTS.md` before editing files in the repository.
- Use English for code, comments, docs, identifiers, commit-style summaries, and test names.
- Prefer small, reviewable changes that keep the firmware build passing.
- Do not introduce temporary workarounds as the final design.
- Do not add new third-party dependencies unless explicitly required and justified.
- Do not commit unless explicitly asked.

## Architecture Direction

The intended RF redesign architecture is:

- `components/radio_cc1101`
  - CC1101 HAL, SPI transport, device/register access, radio profile data, and GDO/IRQ plumbing
  - no protocol parsing or framing logic
- `components/wmbus_tmode_rx`
  - RX owner task/session engine
  - incremental T-mode 3-of-6 framing
  - early candidate rejection
  - exact encoded frame capture contracts
- `components/wmbus_link`
  - link-layer validation
  - CRC/block validation
  - telegram/domain object building
- `components/rf_diagnostics`
  - bounded diagnostics records
  - ring buffer for malformed or anomalous RF sessions
  - retrieval helpers for HTTP/API diagnostics
- `components/mqtt_service`
  - bounded serializers
  - typed publish commands or equivalent bounded transport contracts
  - no heavy forensic payload building in hot paths
- `components/app_core`
  - orchestration only
  - task wiring, lifecycle, and integration
  - not the home for protocol logic

## Hard Constraints

### Radio Ownership

- The CC1101 peripheral must have exactly one owner task.
- ISR handlers must be minimal and must not perform SPI transactions or heavy work.
- GDO interrupts must hand work off to the owner task using an ISR-safe mechanism.
- Keep exactly one active RX runtime path at a time during the refactor.

### Hot-Path Allocation

- Avoid heap-heavy work in runtime-critical paths.
- Avoid large `std::string`, `cJSON_Print*`, or equivalent dynamic payload building in frame-processing hot paths.
- Prefer fixed-size structs, bounded buffers, stack buffers, or clearly bounded pools.

### Diagnostics vs Telemetry

- Do not send full forensic raw-frame dumps through the normal MQTT hot path.
- Valid operational telemetry and valid telegram publishing are separate concerns from RF forensics.
- Malformed or anomalous RF captures should go to a bounded diagnostics subsystem retrievable on demand over HTTP or another bounded diagnostics interface.

### Protocol Layering

- Do not treat arbitrary raw FIFO bursts as valid long-term frame contracts.
- Prefer exact encoded frame capture contracts once the RX and framing logic is in place.
- Keep T-mode 3-of-6 framing logic separate from CC1101 register and device logic.
- Keep link-layer validation separate from radio capture and session logic.

## Testing Expectations

For every non-trivial change:

1. Run targeted tests for the changed module(s).
2. Then run the full firmware build.

If pure logic is added, it should be host-testable whenever reasonably possible.
Prefer extracting pure logic from ESP-IDF glue rather than trying to test IDF-heavy code directly.

## Mandatory Build Command

Always run this exact build command before considering a task complete:

```bash
bash -lc 'cd /home/krzycho/Projekty/water-gateway && source /home/krzycho/.espressif/v5.5.4/esp-idf/export.sh && idf.py build'
```

## Change Discipline

When making changes:

- inspect existing component boundaries and CMake wiring first
- reuse existing patterns when they are sound
- document important assumptions in code comments near the implementation
- update docs when architecture or contracts change
- remove dead code and obsolete compatibility paths once the new design is in place
- do not leave duplicate runtime paths without clear justification

## Preferred Implementation Style

- Prefer explicit state machines for RX and session logic.
- Prefer enums and typed result/status objects over ambiguous booleans.
- Prefer narrow interfaces with clear ownership.
- Prefer bounded, deterministic behavior over convenience abstractions.
- Prefer host-testable pure functions for parsing, decoding, validation, and size calculations.

## What To Avoid

- Do not add speculative timeout tuning as a substitute for correct framing or session design.
- Do not push malformed RF captures through MQTT as large JSON blobs.
- Do not hide architectural problems behind payload truncation while keeping the bad hot path intact.
- Do not move protocol details into `app_core` just to make integration easier.
- Do not let multiple tasks access the radio concurrently.

## Expected Completion Standard

A task is complete only when:

- the code is coherent with the target architecture
- targeted tests pass
- the full ESP-IDF build passes
- docs are updated if contracts or structure changed
- no obvious dead code or contradictory path remains from the edited area
