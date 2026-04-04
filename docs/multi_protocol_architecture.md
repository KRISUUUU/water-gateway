# Multi-Protocol Architecture

## Purpose

This document describes the protocol-driver architecture that enables the
gateway to support multiple Wireless M-Bus protocols over a single CC1101,
with a clear path to a second CC1101 in the future.

It is the companion document to `docs/rf_refactor_target.md`, which describes
the lower-level session-engine and framing refactor. Read that document first.

---

## Current Reality vs Target

| Dimension            | Current                            | Target                                    |
|----------------------|------------------------------------|-------------------------------------------|
| Protocols            | W-MBus T-mode only                 | T-mode + PRIOS (+ future others)          |
| Radios               | One CC1101                         | One CC1101 now; second CC1101 path ready  |
| Protocol selection   | Locked to T-mode (from config)     | Scheduler-managed, profile-switched       |
| Multi-protocol RX    | Locked mode only today             | Time-sliced single-radio profile rotation |
| Protocol driver API  | `IProtocolDriver` (WMbusTmodeDriver complete) | All protocols via `IProtocolDriver`  |
| Scheduler mode       | Locked (wired)                     | Locked / Priority / Scan (data model complete) |
| Profile definitions  | WMbusT868 (active), PriosR3/R4 (placeholders) | Full register data once captures exist |

---

## Key Concept: One Radio, Multiple Protocols via Scheduled Profile Switching

A single CC1101 **cannot receive two protocols simultaneously**. Multi-protocol
support is achieved by rotating through protocol profiles over time:

1. The `RadioProtocolScheduler` holds a list of `ProtocolSlot` entries, each
   binding a `ProtocolId` to a `RadioProfileId` and a `RadioInstanceId`.
2. The radio owner task (future work) cycles through enabled slots for its
   radio instance, applying the required CC1101 register configuration before
   each receive window.
3. Within each receive window, the session engine runs exactly one protocol
   driver.
4. Captured frames are handed to the matching `IProtocolDriver` for framing
   and link validation.

This is scheduled/managed multi-protocol receive, **not simultaneous receive
on multiple frequencies or profiles**.

---

## Component: `protocol_driver`

### Location

`components/protocol_driver/`

### Responsibilities

- Define protocol-layer identity types (`ProtocolId`, `RadioInstanceId`,
  `RadioProfileId`).
- Define the radio profile descriptor (`RadioProfile`) as a protocol-layer
  view of a CC1101 configuration.
- Define the output types for the receive pipeline:
  - `ProtocolFrame` — the output of a completed RX capture session before
    link validation (Contract A equivalent).
  - `DecodedTelegram` — the normalized output after successful link validation
    (Contract C equivalent).
- Define the `IProtocolDriver` interface that all concrete protocol drivers
  must implement.
- Provide the `RadioProtocolScheduler` data model for managing slot
  assignments.

### Non-Responsibilities

- No SPI or GPIO access.
- No FreeRTOS task or queue management.
- No MQTT or HTTP transport concerns.
- No CC1101 register constants (those stay in `radio_cc1101`).

### Dependencies

- `common` only.

---

## Interface: `IProtocolDriver`

Each protocol is encapsulated in a class implementing `IProtocolDriver`:

```
IProtocolDriver
├── protocol_id()             → ProtocolId
├── required_radio_profile()  → RadioProfileId
├── max_session_encoded_bytes() → size_t
├── reset_session()
├── feed_byte(uint8_t)        → DriverFeedResult
│     status: NeedMoreData | CandidateViable | FrameComplete | FrameRejected
├── finalize_frame(out)       → bool   (when FrameComplete)
└── decode_telegram(frame, out) → bool (link validation + telegram building)
```

The session engine calls `feed_byte()` for each byte drained from the CC1101
FIFO. When `FrameComplete` is signalled, the engine calls `finalize_frame()`
and then `decode_telegram()`.

The session engine does not know which protocol is in use; it only holds a
pointer to the active `IProtocolDriver`.

---

## Concrete Protocol Drivers (Migration Targets)

### `WMbusTmodeDriver`

- **Location:** `components/wmbus_link/include/wmbus_link/wmbus_tmode_driver.hpp`
  and `components/wmbus_link/src/wmbus_tmode_driver.cpp`
- **Component rationale:** Lives in `wmbus_link` rather than `wmbus_tmode_rx` to
  avoid a circular dependency. `wmbus_link` already depends on `wmbus_tmode_rx`;
  the reverse direction would be a cycle.
- **Protocol:** W-MBus T1/T2 mode (Protocol Driver #1)
- **Profile:** `RadioProfileId::WMbusT868` (868.95 MHz OOK, 32.768 kbaud)
- **Status:** Implemented. Wraps `WmbusTmodeFramer` + `WmbusLink::validate_and_build()`.
  The `RxSessionEngine` still drives FIFO capture; the driver is used for
  finalization and decode. The queue between `radio_rx_task` and `pipeline_task`
  now carries a `RadioRxQueueItem` with `ProtocolId`, `RadioInstanceId`, and
  `RadioProfileId` alongside the `EncodedRxFrame`.
- **Bridge accessor:** `last_validated_telegram()` exposes the
  `wmbus_link::ValidatedTelegram` for callers (router, MQTT) that have not yet
  migrated to `DecodedTelegram`.

### `WMbusPriosDriver`

- **Location:** `components/wmbus_prios_rx` (to be created)
- **Protocol:** PRIOS / Sensus variant
- **Profile:** `RadioProfileId::WMbusPrios868` (exact RF params TBD)
- **Status:** Scaffolding only. No decoding implemented yet. A capture/analyzer
  fixture must be built from real hardware captures before any decoder is
  written. See global rules in the prompt context: do not invent PRIOS field
  semantics without evidence.

---

## Component: `RadioProfileManager`

`RadioProfileManager` is a singleton in `protocol_driver` that manages the
active CC1101 profile and scheduler state for one radio instance at runtime.

### Scheduler modes

| Mode     | Behaviour                                                           |
|----------|---------------------------------------------------------------------|
| Locked   | One profile selected at startup; radio never leaves it. (Wired.)    |
| Priority | Preferred profile + bounded fallback scan. (Scaffolding only.)      |
| Scan     | Round-robin across all enabled profiles. (Scaffolding only.)        |

### Configuration (v3+)

`AppConfig.radio` carries two new fields:

- `scheduler_mode` (`RadioSchedulerMode`): default Locked.
- `enabled_profiles` (`RadioProfileMask`): bitmask of active profiles,
  default `kRadioProfileMaskWMbusT868`. Must not be zero.

`migrate_v2_to_v3()` applies these defaults for existing stored configs.

### Diagnostics exposure

`GET /api/diagnostics/radio` now includes a `scheduler` object:

```json
"scheduler": {
  "mode": "Locked",
  "active_profile": "WMbusT868",
  "enabled_profiles": ["WMbusT868"],
  "last_switch_reason": "Initial",
  "profile_switch_count": 0,
  "irq_wake_count": 1234,
  "fallback_wake_count": 5
}
```

IRQ-first vs fallback-poll wake counts are recorded by the radio owner task
on every `wait_for_radio_rx_work()` return and stored in the manager.

---

## Radio Profile Definitions

| Profile          | Status     | RF Parameters                            |
|------------------|------------|------------------------------------------|
| WMbusT868 (id=1) | Active     | 868.95 MHz OOK, 32.768 kbaud             |
| WMbusPriosR3 (id=2) | Placeholder | 868 MHz OOK, exact params TBD        |
| WMbusPriosR4 (id=3) | Placeholder | 868 MHz OOK, exact params TBD        |

PRIOS R3 and R4 are different radio sub-variants of the PRIOS protocol.
Their exact CC1101 register tables must be derived from real hardware captures
before any register data is committed.

---

## Type: `RadioProtocolScheduler`

`RadioProtocolScheduler` is a pure data model (no FreeRTOS, no SPI). It holds
up to `kMaxProtocolSlots` (currently 8) `ProtocolSlot` entries.

A `ProtocolSlot` contains:

```
ProtocolSlot
├── protocol        : ProtocolId
├── profile         : RadioProfileId
├── radio_instance  : RadioInstanceId
├── enabled         : bool
└── slot_duration_ms: uint32_t   (0 = default; unused until time-sliced scheduler)
```

The scheduler is **not yet wired to the runtime owner task**. It is
instantiated in the owner task setup code once migration proceeds. For now it
exists to allow configuration and testing of the scheduling model in isolation.

---

## Two-Radio Expansion Path

When a second CC1101 is added:

1. Assign `RadioInstanceId = 1` to it.
2. Register `ProtocolSlot` entries with `radio_instance = kRadioInstanceSecondary`.
3. The second radio runs its own owner task (independent of the first).
4. Both tasks share the same `IProtocolDriver` implementations; the captured
   frame metadata includes `radio_instance` so routing and diagnostics remain
   unambiguous.

Protocol logic (`IProtocolDriver` implementations, link validators, telegram
builders) is **radio-instance-agnostic**. No protocol code needs to change to
support the second radio.

The scheduler correctly returns independent `active_slot_for_radio()` results
for each instance.

---

## Output Types

### `ProtocolFrame`

Produced by the session engine when a complete encoded frame is captured.
Contains:

- Encoded and decoded byte arrays (bounded: 290 encoded / 256 decoded).
- Signal quality (RSSI, LQI, radio CRC).
- Metadata: protocol, radio instance, profile, capture elapsed time.
- `FrameCaptureEndReason` explaining how the capture ended.
- `frame_complete` flag (true only when the exact encoded byte budget was met).

Corresponds to Contract A in `docs/rf_refactor_target.md`.

### `DecodedTelegram`

Produced by `IProtocolDriver::decode_telegram()` after link validation.
Contains:

- `DecodedTelegramIdentity`: device ID, manufacturer ID, device type/version,
  reliability flag.
- `DecodedTelegramMetadata`: radio instance, profile, protocol, signal quality,
  timestamp, frame size summary.
- `canonical_bytes`: application payload ready for dedup keying and downstream
  consumption. CRC bytes are stripped; PHY encoding is reversed.

Corresponds to Contract C in `docs/rf_refactor_target.md`.

---

## Migration Order (Protocol Layer)

The migration from the current implicit T-mode path to the driver architecture
follows these steps (each step is a separate prompt/PR):

1. ✅ **Scaffolding**: `protocol_driver` component — types, interfaces, scheduler.
2. ✅ **T-mode Driver #1**: `WMbusTmodeDriver` wrapping `WmbusTmodeFramer` +
   `WmbusLink::validate_and_build()`. Protocol identity threaded through the
   frame queue via `RadioRxQueueItem`.
3. ✅ **Scheduler layer**: `RadioProfileManager` singleton, scheduler mode +
   enabled-profiles in config (v3), IRQ/fallback wake counters, API exposure
   in `GET /api/diagnostics/radio`, PRIOS R3/R4 placeholder profile IDs.
4. Wire the `RxSessionEngine` owner task to use `IProtocolDriver::feed_byte()`
   directly (replacing internal framer wiring).
5. Switch the pipeline task to consume `DecodedTelegram` instead of
   `ValidatedTelegram` (update router, MQTT, diagnostics).
6. Remove legacy `encode_session_capture()` adapter and direct
   `WmbusLink::validate_and_build()` calls from `runtime_tasks.cpp`.
7. Create `wmbus_prios_rx` component with a stub `WMbusPriosDriver` and build
   a hardware capture fixture.
8. Register PRIOS in the scheduler once its RF parameters are confirmed.

---

## Testing

Pure protocol-driver logic is host-testable.

### `tests/host/test_protocol_driver.cpp`

Covers:

- `ProtocolId` and `RadioProfileId` enum values and string labels
- `RadioInstanceId` constants
- `RadioProfile` validity
- `RadioProtocolScheduler`: add, remove, active-slot query, slot limit,
  disabled-slot handling, insertion-order preservation, two-radio independence,
  and clear.

`IProtocolDriver` is an interface; concrete driver tests belong in the
respective component test files (`test_wmbus_tmode_framer.cpp`,
`test_rx_session_engine.cpp`, etc.).

### `tests/host/test_wmbus_tmode_driver.cpp`

Covers the full `WMbusTmodeDriver` concrete implementation:

- Identity contracts (`protocol_id`, `required_radio_profile`, `max_session_encoded_bytes`)
- `reset_session` clears all state
- `feed_byte` maps framer states to `DriverFeedStatus` correctly
- `CandidateViable` status seen before `FrameComplete`
- `finalize_frame` fills `ProtocolFrame` with correct encoded/decoded content,
  protocol metadata, and `frame_complete` flag
- `decode_telegram` produces a valid `DecodedTelegram` with correct identity,
  signal quality pass-through, canonical bytes, and device type/version
- `last_validated_telegram()` bridge to `wmbus_link::ValidatedTelegram`
- `decode_telegram` returns false without prior `finalize_frame`
- Reset → second session produces identical correct decode
