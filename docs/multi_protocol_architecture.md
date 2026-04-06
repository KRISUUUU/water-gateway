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
| Protocols            | T-mode + PRIOS R3 capture runtime  | T-mode + PRIOS (+ future others)          |
| Radios               | One CC1101                         | One CC1101 now; second CC1101 path ready  |
| Protocol selection   | Scheduler-managed, profile-switched| Scheduler-managed, profile-switched       |
| Multi-protocol RX    | Single-radio runtime with explicit bounded profile switching | Time-sliced single-radio profile rotation |
| Protocol driver API  | `IProtocolDriver` (WMbusTmodeDriver complete) | All protocols via `IProtocolDriver`  |
| Scheduler mode       | Locked / Priority / Scan (wired)   | Locked / Priority / Scan + multi-radio expansion |
| Profile definitions  | WMbusT868 (active), PriosR3/R4 (placeholders) | Full register data once captures exist |

---

## Key Concept: One Radio, Multiple Protocols via Scheduled Profile Switching

A single CC1101 **cannot receive two protocols simultaneously**. Multi-protocol
support is achieved by rotating through protocol profiles over time:

1. The `RadioProtocolScheduler` holds a list of `ProtocolSlot` entries, each
   binding a `ProtocolId` to a `RadioProfileId` and a `RadioInstanceId`.
2. The radio owner task cycles through enabled slots for its radio instance,
   applying the required CC1101 register configuration explicitly on bounded
   idle/liveness wakes.
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
- **Status:** Experimental capture runtime only. PRIOS R3 can be scheduled on
  the primary radio and uses bounded bring-up/discovery capture paths. PRIOS R4
  remains a declared profile ID for scheduler/runtime planning, but no dedicated
  R4 RX path is active yet.

---

## Component: `RadioProfileManager`

`RadioProfileManager` is a per-radio-instance runtime object in
`protocol_driver` that manages scheduler state for one radio instance at
runtime. The primary and secondary radio instances have separate managers.

### Scheduler modes

| Mode     | Behaviour                                                           |
|----------|---------------------------------------------------------------------|
| Locked   | One preferred profile selected at startup; the radio stays on it.   |
| Priority | Preferred profile with bounded fallback scan, then explicit return. |
| Scan     | Round-robin across all enabled profiles on bounded idle wakes.      |

### Configuration (Persisted + Runtime)

`AppConfig.radio` carries two new fields:

- `scheduler_mode` (`RadioSchedulerMode`): default Locked.
- `enabled_profiles` (`RadioProfileMask`): bitmask of active profiles,
  default `kRadioProfileMaskWMbusT868`. Must not be zero.

`migrate_v2_to_v3()` applies these defaults for existing stored configs.

At runtime, the owner task expands the persisted single-radio settings into a
`RadioRuntimePlan` with one active primary instance and one reserved secondary
slot. That keeps the deployed configuration backward-compatible while making
the scheduler/runtime plumbing instance-safe for a future second CC1101.

### Diagnostics exposure

`GET /api/diagnostics/radio` now includes a `scheduler` object:

```json
"scheduler": {
  "mode": "Locked",
  "preferred_profile": "WMbusT868",
  "selected_profile": "WMbusT868",
  "active_profile": "WMbusT868",
  "active_protocol": "WMBUS_T",
  "last_apply_status": "Applied",
  "enabled_profiles": ["WMbusT868"],
  "last_switch_reason": "Initial",
  "last_wake_source": "IrqNotification",
  "profile_switch_count": 0,
  "profile_apply_count": 1,
  "profile_apply_failures": 0,
  "irq_wake_count": 1234,
  "fallback_wake_count": 5
}
```

IRQ-first vs fallback-poll wake counts are recorded by the radio owner task
on every `wait_for_radio_rx_work()` return and stored in the manager. A
separate `topology` object reports that the current deployment is single-radio
with the secondary CC1101 slot reserved but inactive.

The diagnostics endpoint also exposes an operator-oriented summary:

- active profile and active protocol
- last wake source (`IrqNotification` vs `FallbackPoll`)
- recent successful T-mode telegrams
- last bounded T-mode reject reason from `rf_diagnostics`
- recent successful PRIOS identity captures
- PRIOS support level (`identity_only_capture`)
- last bounded PRIOS reject reason from `PriosCaptureService`

---

## Radio Profile Definitions

| Profile          | Status     | RF Parameters                            |
|------------------|------------|------------------------------------------|
| WMbusT868 (id=1) | Active     | 868.95 MHz OOK, 32.768 kbaud             |
| WMbusPriosR3 (id=2) | Experimental capture runtime | 868 MHz OOK, discovery/campaign bring-up profiles |
| WMbusPriosR4 (id=3) | Presence only | Reserved profile ID; dedicated RX path not active yet |

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

This pure scheduler model remains useful for reasoning and tests, but the
single-radio owner task is now wired through `RadioProfileManager` and explicit
profile-apply helpers rather than using `RadioProtocolScheduler` directly.

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

## Practical Hardware Test Plan

The current implementation is designed for bounded, operator-visible hardware
validation on one CC1101. Use the Diagnostics page and `GET /api/diagnostics/radio`
as the primary source of truth while testing.

### 1. T-mode validation

1. In Settings, leave PRIOS experimental modes off.
2. Enable `WMbusT868` in the enabled-profile list.
3. Use `Locked` mode first.
4. Confirm in diagnostics:
   - `Active Profile = WMbusT868`
   - `Active Protocol = WMBUS_T`
   - `Last Wake Source` changes over time
   - `T-mode Recent Accepts` rises near known T-mode meters
   - `T-mode Last Success` shows bounded RSSI/LQI and a meter key
5. If T-mode rejects happen, use `T-mode Last Reject` plus the bounded
   `rf_diagnostics` records to understand why without enabling giant dumps.

### 2. PRIOS R3 validation

1. Enable `WMbusPriosR3` in Settings.
2. Choose the desired scheduler mode or enable a PRIOS experimental lock mode.
3. In Diagnostics, confirm:
   - `PRIOS Mode` is the expected experimental mode
   - `Profile = WMbusPriosR3`
   - `Support Level = identity_only_capture`
   - `Reading Decode = not available`
   - capture/reject counters move in bounded ways
   - `PRIOS Last Reject` explains the most recent capture gate decision
   - `PRIOS Last Success` shows the last retained identity hit with RSSI/LQI
4. Use the export button for the bounded retained capture set when offline
   analysis is needed.

### 3. PRIOS R4 validation

PRIOS R4 is present only as a scheduler/runtime profile ID today.

Validation goal:

- confirm that `WMbusPriosR4` can exist in config/runtime planning
- confirm the runtime reports the request clearly
- confirm unsupported application falls back safely instead of pretending R4
  decode exists

Expected result on one radio today:

- requested `WMbusPriosR4`
- applied fallback profile visible in diagnostics
- no fake R4 RX path

### 4. Scheduler validation on one radio

Use `GET /api/diagnostics/radio` and the Diagnostics page to verify:

- `Locked`: selected/active profile stay fixed
- `Priority`: preferred profile is primary, bounded fallback happens, then
  returns explicitly
- `Scan`: selected/active profile rotate across enabled profiles
- `Last Switch Reason` explains why the last transition happened
- `Profile Apply Status` shows whether the requested profile actually applied
- only one active RX path exists at a time on the primary CC1101

### 5. What changes when a second radio is added later

What stays the same:

- protocol IDs
- profile IDs
- protocol drivers
- bounded diagnostics model
- operator-facing status concepts (`active profile`, `active protocol`,
  `last wake source`, per-protocol successes/rejects)

What changes:

- a second `RadioProfileManager` instance becomes active
- a second owner task can run on the new CC1101
- the runtime plan reports two present radio instances instead of one active
  plus one reserved slot
- scheduler/runtime diagnostics become per-instance rather than primary-only

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
3. ✅ **Scheduler layer**: per-instance `RadioProfileManager`, scheduler mode +
   enabled-profiles in config (v3), IRQ/fallback wake counters, explicit
   selected/applied profile tracking, API exposure in
   `GET /api/diagnostics/radio`, PRIOS R3/R4 profile IDs, and single-radio
   runtime topology expansion.
4. Wire the `RxSessionEngine` owner task to use `IProtocolDriver::feed_byte()`
   directly (replacing internal framer wiring).
5. Switch the pipeline task to consume `DecodedTelegram` instead of
   `ValidatedTelegram` (update router, MQTT, diagnostics).
6. Remove legacy `encode_session_capture()` adapter and direct
   `WmbusLink::validate_and_build()` calls from `runtime_tasks.cpp`.
7. Continue PRIOS evidence gathering on the experimental capture path until the
   real on-air framing contract is known.
8. Replace PRIOS bring-up capture with a dedicated protocol driver only after
   the RF/framing evidence is stable.

---

## Testing

Pure protocol-driver logic is host-testable.

### `tests/host/test_protocol_driver.cpp`

Covers:

- `ProtocolId` and `RadioProfileId` enum values and string labels
- `RadioInstanceId` constants
- `RadioProfile` validity
- `RadioRuntimePlan` single-radio expansion
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
