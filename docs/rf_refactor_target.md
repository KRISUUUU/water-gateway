# RF Refactor Target

## Purpose

This document defines the target architecture for the CC1101 + Wireless M-Bus T-mode receive
path in this repository.

The redesign goal is to replace the current oversized raw-burst model with a deterministic,
IRQ-driven, session-based RX architecture that:

- captures exact encoded PHY frames instead of arbitrary raw bursts
- performs early rejection of false sync and malformed candidates
- separates radio, framing, link validation, diagnostics, and transport concerns
- removes heap-heavy forensic payload building from runtime hot paths
- exposes malformed-session diagnostics through a bounded subsystem instead of the normal MQTT path

## Current Architecture Summary

The current receive flow is:

1. `components/radio_cc1101`
   - configures CC1101 in infinite packet mode
   - polls `RXBYTES`
   - drains arbitrary raw FIFO bursts after sync
   - stops capture based on timeout, empty polls, overflow, or size limits
2. `components/wmbus_link`
   - validates exact encoded frame contracts
   - performs link-layer CRC/block validation
   - builds the validated telegram/domain object consumed downstream
3. `components/app_core/src/runtime_tasks.cpp`
   - routes validated telegrams
   - updates registry and dedup state
   - enqueues bounded MQTT publish commands
4. `components/mqtt_service`
   - serializes JSON payloads with dynamic strings and `cJSON`
5. `components/diagnostics_service` and HTTP handlers
   - expose live counters and snapshots
   - expose bounded malformed-session history via `rf_diagnostics`

### Current Pain Points

- RX frame boundary is determined too late.
- False sync and trailing RF noise can produce oversized captures.
- 3-of-6 decode happens too late, after too much data has already been captured.
- Malformed RF captures flow too far downstream.
- The hot path still includes heap-heavy payload construction for diagnostics.
- MQTT is being used for forensic payloads that do not belong in the normal telemetry path.
- Radio ownership boundaries are not yet clean enough for the intended architecture.

## Target Architecture

The target receive pipeline is:

```text
CC1101 HAL / Device / IRQ
    ↓
W-MBus T-Mode RX Session Engine
    ↓
Incremental 3-of-6 Framer
    ↓
W-MBus Link Validator
    ↓
Telegram Builder / Domain Model
    ↓
Router / Registry / Application Logic
    ├── Bounded MQTT publish path
    └── RF diagnostics ring buffer + HTTP/API retrieval
```

## Intended Component Boundaries

### `components/radio_cc1101`

Responsibilities:

- SPI transport
- CC1101 register access
- strobe commands
- FIFO read and write primitives
- profile/config application
- GDO/IRQ plumbing
- reset and recovery primitives

Non-responsibilities:

- no W-MBus protocol parsing
- no 3-of-6 decode
- no link-layer validation
- no MQTT diagnostics formatting

Key rule:

- the radio peripheral has one owner task

### `components/wmbus_tmode_rx`

Responsibilities:

- IRQ-first receive session handling with a bounded active-session watchdog tick
- rare idle fallback wake only for liveness / missed-notification recovery
- FIFO draining during RX sessions
- incremental T-mode 3-of-6 framing
- parallel normal and reversed-bit candidate tracking
- early L-field discovery
- early malformed-candidate rejection
- exact encoded frame completion
- bounded session outcome reporting

Non-responsibilities:

- no MQTT publishing
- no app-level routing
- no long-term diagnostics storage

### `components/wmbus_link`

Responsibilities:

- link-layer validation
- CRC and block validation
- decoded telegram building
- identity extraction
- canonical representation
- dedup-relevant framing outputs

Non-responsibilities:

- no direct radio access
- no GDO/IRQ logic
- no HTTP or MQTT transport concerns

### `components/rf_diagnostics`

Responsibilities:

- bounded fixed-size diagnostics records
- ring buffer of malformed or anomalous RF sessions
- compact metadata and short raw or decoded prefixes only
- retrieval helpers for HTTP/API handlers

Non-responsibilities:

- no full forensic MQTT streaming in hot paths
- no large heap-allocated payload generation in frame-processing tasks

### `components/mqtt_service`

Responsibilities:

- bounded serializers
- publish transport
- typed publish commands or equivalent bounded contracts
- valid operational telemetry and valid telegram publishing

Non-responsibilities:

- no heavy forensic raw dump building in the normal runtime path
- no protocol parsing

### `components/app_core`

Responsibilities:

- task creation
- lifecycle orchestration
- queue and service wiring
- high-level integration only

Non-responsibilities:

- no deep protocol parsing
- no radio-specific framing logic
- no large dynamic diagnostic payload building

## Core Contracts

### Contract A: Exact Encoded Frame Capture

The RX layer must output an object representing exactly one candidate encoded PHY frame payload.

Required properties:

- starts at the first encoded byte relevant to the W-MBus payload
- excludes arbitrary trailing RF noise
- excludes unbounded raw-burst semantics
- contains metadata required for diagnostics and downstream validation
- is bounded and deterministic

Required metadata should include at least:

- timestamp
- bit orientation used
- RSSI and LQI snapshots
- session end reason
- expected encoded size
- actual encoded size
- early reject reason when the candidate is invalid

### Contract B: Incremental Framer Status

The framer must support progressive feeding and return explicit typed states such as:

- need more data
- candidate rejected
- candidate viable
- exact frame complete

Whole-buffer decode may remain a temporary compatibility path during migration, but it must not be
the final primary contract.

### Contract C: Valid Telegram Domain Object

The downstream link layer must produce a domain object only when:

- framing is complete
- structure is valid
- required checks pass
- identity and canonical data are coherent

Malformed sessions must not be represented as normal telegrams.

### Contract D: Diagnostics Record

Diagnostics records must be fixed-size or effectively bounded.

They should include:

- reason code
- relevant lengths
- timestamps
- radio metadata
- small raw and/or decoded prefixes
- limited context fields needed for debugging

They must not contain arbitrarily large full-frame strings in runtime-critical paths.

### Contract E: Typed Publish Command

The transport boundary should accept typed publish commands rather than ad hoc string construction
inside the pipeline task.

The command contract should make it explicit whether the payload represents:

- validated telegram telemetry
- operational status or health telemetry
- bounded warning or event notifications

Malformed-session forensics belong in diagnostics storage and retrieval, not normal hot-path MQTT.

## RX Strategy

### Session-Based RX

The target RX model is session-based, not polling-burst-based.

Suggested owner-task state model:

- `armed`
- `synced`
- `seeding`
- `candidate_tracking`
- `exact_capture`
- `finalize`
- `abort_or_recover`

### IRQ Strategy

Use GDO interrupts to wake the owner task.
The ISR must remain minimal and only signal work.

Suggested split:

- one signal for sync or packet activity
- one signal for FIFO threshold work

### Framing Strategy

The session engine feeds encoded bytes into an incremental 3-of-6 framer.
The framer:

- tracks both orientations
- discovers the L-field early
- rejects impossible candidates early
- determines the exact encoded byte budget once enough decoded state is known

### Packet-Length Strategy

Preferred design:

- begin in infinite packet mode
- once enough framing information is known, optionally transition to fixed-length capture for the
  remaining bytes when safe
- keep software validation authoritative even if hardware packet-length features are used

Current implementation detail:

- the switch decision is centralized in pure session-engine helper logic
- the session engine only attempts the handoff once the framer knows the exact remaining encoded
  byte count and that remainder fits in the CC1101 fixed-length register
- software still clamps reads to the exact remaining encoded budget even after the hardware mode
  switch
- the CC1101-specific assumption is that programming fixed-length mode with the remaining encoded
  tail during an active receive session safely bounds the rest of the capture; this still requires
  hardware bench validation

## Migration Direction

The redesign should proceed in buildable slices:

1. extract host-testable pure framing logic
2. introduce exact-frame and link-layer contracts
3. add bounded RF diagnostics storage
4. move MQTT toward typed bounded publish commands
5. split radio HAL/device/profile concerns from higher-level RX logic
6. add IRQ plumbing for the owner task
7. switch to the session engine as the only active RX runtime path
8. remove obsolete raw-burst compatibility paths promptly

During migration, keep exactly one active RX runtime path at a time and avoid long-lived duplicate
architectures.
