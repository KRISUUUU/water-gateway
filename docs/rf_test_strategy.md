# RF Test Strategy

## Purpose

This document defines the testing strategy for the CC1101 / Wireless M-Bus RX refactor.

The main principles are:

- pure protocol and framing logic should be host-testable
- ESP-IDF glue should stay thin
- runtime-critical paths should remain bounded and deterministic
- firmware build validation is mandatory after each meaningful change

## Design Rules For Testable Modules

The following modules should remain as independent from ESP-IDF as reasonably possible:

- `wmbus_tmode_rx` pure framer and session-decision helpers
- `wmbus_link` validation, CRC/block helpers, telegram building, and canonicalization
- `rf_diagnostics` ring buffer behavior and bounded formatting helpers
- `mqtt_service` bounded serializer helpers and typed publish-command mapping
- small state-machine and policy helpers in `radio_cc1101` that do not require SPI/GPIO access

Keep ESP-IDF-specific code thin and isolate it behind adapters wherever practical.

## Test Layers

### 1. Host-Side Pure Logic Tests

Use host-side tests for logic that does not require ESP-IDF runtime or real hardware.

Target areas:

- incremental 3-of-6 decoder
- orientation tracking
- encoded-length calculations
- candidate rejection logic
- early L-field handling
- link-layer validation
- CRC/block validation helpers
- telegram building
- bounded serializers
- diagnostics ring buffer behavior
- small owner-task decision/state helpers that can run with fakes

Preferred characteristics:

- no direct ESP-IDF dependencies
- deterministic inputs and outputs
- no SPI/GPIO access
- no timing dependence on FreeRTOS

### 2. Thin Integration / Adapter Tests

Where possible, test adapter boundaries with fakes or lightweight stubs:

- fake IRQ event source
- fake CC1101 device abstraction
- fake FIFO provider
- fake serializer sink

These tests should verify:

- owner-task state transitions
- event handoff behavior
- packet-mode decisions
- recovery decisions

### 3. Full Firmware Build Validation

After each non-trivial change, the firmware must build successfully.

Mandatory build command:

```bash
bash -lc 'cd /home/krzycho/Projekty/water-gateway && source /home/krzycho/.espressif/v5.5.4/esp-idf/export.sh && idf.py build'
```

This is required even when host tests pass.

### 4. On-Device Validation

Some behavior cannot be fully proven without real hardware.

Examples:

- GDO timing and IRQ behavior
- SPI timing margins
- FIFO threshold tuning
- CC1101 mode transitions on real traffic
- overflow and recovery behavior under RF noise
- real RSSI/LQI behavior

These checks should be limited to hardware-semantic validation after the software architecture is
already correct.

## Existing Host Test Harness

This repository already includes a host-side test harness under `tests/host`.

Key characteristics:

- CMake + CTest based harness
- one executable per test source
- component `.cpp` files compiled directly into host tests
- `HOST_TEST_BUILD` preprocessor define used to gate ESP-IDF-specific code
- `tests/host/host_test_stubs.hpp` provides lightweight stubs for ESP-IDF types and macros

Current entry points:

- [tests/README.md](/home/krzycho/Projekty/water-gateway/tests/README.md)
- [tests/host/CMakeLists.txt](/home/krzycho/Projekty/water-gateway/tests/host/CMakeLists.txt)

### Running The Existing Host Tests

```bash
cd /home/krzycho/Projekty/water-gateway/tests/host
mkdir -p build
cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

When a build directory already exists, the short form is:

```bash
cd /home/krzycho/Projekty/water-gateway/tests/host/build
ctest --output-on-failure
```

### Adding New Pure-Logic Tests

When adding a new host-testable module:

1. add a `test_*.cpp` file under `tests/host`
2. register a new executable in `tests/host/CMakeLists.txt`
3. compile only the pure or lightly-adapted source files needed by that test
4. keep ESP-IDF dependencies behind `HOST_TEST_BUILD` guards or thin adapters

Do not introduce a second parallel host-test framework unless there is a strong reason.

## Test Priorities By Module

### `radio_cc1101`

Host-test what is practical:

- configuration/profile helper logic
- mode decision helpers
- non-IDF helper functions

Do not overfit tests around ESP-IDF-specific SPI/GPIO internals.

### `wmbus_tmode_rx`

This should carry the heaviest host-side test coverage.

Must be testable:

- incremental feed behavior
- normal and reversed orientation candidate tracking
- early L-field detection
- reject reasons
- exact encoded-frame completion
- packet-length decision logic

### `wmbus_link`

Must be strongly host-tested:

- CRC/block validation
- decoded frame acceptance and rejection
- manufacturer/device identity extraction
- canonical representation
- dedup-relevant outputs

### `rf_diagnostics`

Must be host-tested:

- ring buffer insertion
- wraparound behavior
- record eviction order
- bounded output behavior
- API-facing formatting helpers when they are pure

### `mqtt_service`

Must be tested for:

- bounded serializer behavior
- upper size guarantees
- no unbounded forensic payload construction in the hot path
- typed publish-command mapping

### `app_core`

Prefer lighter integration coverage.
Keep logic here small enough that extensive direct testing is not needed.

## Test Design Rules

- Prefer explicit fixtures over large implicit setup.
- Prefer enums and typed status results over boolean ambiguity.
- Keep test inputs small and representative.
- Include malformed cases, not only valid cases.
- Cover edge conditions such as:
  - too short
  - too long
  - invalid symbol
  - impossible length
  - orientation mismatch
  - partial block
  - exact-boundary completion
  - bounded storage wraparound
- Avoid tests that merely duplicate implementation structure without validating behavior.

## Minimum Coverage Expectations For The Refactor

Before the redesign is considered complete, the following behaviors should be covered where
possible:

### Framing

- valid normal-orientation frame
- valid reversed-orientation frame
- invalid symbol handling
- impossible L-field rejection
- exact encoded-length computation
- candidate completion at the exact boundary
- false-candidate rejection before large capture

### Link Validation

- valid frame accepted
- invalid frame rejected
- correct identity extraction
- correct canonical representation

### Diagnostics

- malformed session stored as a bounded record
- ring buffer wraparound behavior
- no large dynamic forensic payload generation required for storage

### MQTT / Serialization

- valid publish payload stays bounded
- no giant malformed-frame forensic payload built in the hot path
- typed publish command to payload conversion is correct

### Runtime Integration

- owner-task RX path builds and wires correctly
- diagnostics path builds and wires correctly
- firmware build passes

## Test Execution Workflow

For every meaningful change:

1. run targeted tests for the changed modules
2. fix failures before moving on
3. run the full firmware build

Recommended order:

- module-level tests first
- integration-adjacent tests second
- full firmware build last

## On-Device Check List

The following items still require device-level confirmation during the refactor:

- chosen GDO interrupt semantics match the intended session engine behavior
- owner-task interrupt handoff is reliable under real traffic
- FIFO threshold configuration does not cause overflow during long frames
- recovery logic behaves correctly under RF noise and malformed traffic
- any optional fixed-length handoff is safe on real hardware

## Final Validation Philosophy

Software work is complete when:

- targeted module tests pass
- the full build passes
- contracts are coherent
- remaining uncertainty is limited to real hardware timing and RF behavior

The refactor should not depend on hope or ad hoc on-device debugging for basic architectural
correctness.
