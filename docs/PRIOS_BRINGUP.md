# PRIOS R3 Bring-up Workflow

This document describes how to run the PRIOS capture campaign, collect raw
captures from hardware, export them as fixtures, analyse them with
`PriosAnalyzer`, and determine when you have enough evidence to write a frame
decoder.

---

## Current Status — Framer BLOCKED

No PRIOS hardware captures exist in this repo yet.  A framer **must not** be
written until the facts below are confirmed from real hardware evidence.

| Fact | Status | Why it blocks the framer |
|------|--------|--------------------------|
| **Sync word** (SYNC1/SYNC0) | **UNKNOWN** | CC1101 profile uses T-mode placeholder `0x54/0x3D` — almost certainly wrong. Without the correct sync word, the CC1101 will not trigger on PRIOS frames and all captured bytes are noise. |
| **Frame-length field** (offset + width) | **UNKNOWN** | Without this the driver cannot know when a frame ends. Fixed-budget capture is the only safe alternative. |
| **CRC scheme** (algorithm, polynomial, byte range) | **UNKNOWN** | Without CRC validation, every noise burst that passes the sync filter looks like a valid frame. |
| **Modulation** (plain 2-FSK vs Manchester-encoded 2-FSK) | **UNKNOWN** | If Manchester is enabled in hardware but disabled in the CC1101 config, every decoded byte is wrong. |
| Baud rate | **UNCONFIRMED** | Current value is the T-mode rate (32.768 kbaud); may be correct but unverified. |

The machine-readable version of this checklist is
`tests/host/test_prios_framer_readiness.cpp`.  Update `kSyncWordKnown` etc.
and commit fixture data when each fact is resolved.

---

## 1. Overview

PRIOS R3 is a proprietary Wireless M-Bus variant.  Its register-level CC1101
configuration and framing are not publicly documented.  The bring-up strategy
is therefore evidence-first:

1. Run the gateway in **capture mode** near a known PRIOS meter.
2. Export the bounded raw captures via the API.
3. Paste them into a host-test fixture file.
4. Use `PriosAnalyzer` to identify sync bytes, common prefix length, length
   distribution, and signal quality.
5. When `Readiness::ready_for_decoder == true`, start writing framing logic and
   replay the fixture against it in a host test.

No PRIOS semantics are invented at any stage — all analysis is statistical and
operates on raw byte streams only.

---

## 2. CC1101 Configuration (EXPERIMENTAL)

The PRIOS R3 radio profile is defined in
`components/radio_cc1101/include/radio_cc1101/cc1101_profile_prios_r3.hpp`.

**All values in that file are EXPERIMENTAL placeholders** derived from the
wM-Bus T-mode base profile.  They have not been verified against real PRIOS
hardware.  In particular:

- Centre frequency: 868.95 MHz (unconfirmed)
- Data rate: 32.768 kBaud (unconfirmed)
- Sync word: T-mode placeholder — **MUST be updated after first captures**
- SYNC_MODE: 001 (15/16 matching bits) — more permissive than T-mode to catch
  unfamiliar framing

Update the register values once real captures reveal the correct sync word and
modulation parameters.

---

## 3. Capture Campaign Mode

### Why a dedicated campaign mode?

Running PRIOS alongside the normal T-mode scan splits radio time and makes it
harder to accumulate clean captures quickly.  **Campaign mode** locks the radio
exclusively to the PRIOS R3 profile for the duration of a capture session,
suspending normal T-mode operation.

Campaign mode also controls the **Manchester encoding variant**:

| Variant | Config flag | CC1101 MDMCFG2 | Description |
|---------|-------------|-----------------|-------------|
| A | `prios_manchester_enabled = false` | `0x01` | Manchester OFF (plain 2-FSK) |
| B | `prios_manchester_enabled = true`  | `0x09` | Manchester ON |

Collect at least 10 captures from **both variants** before drawing conclusions.
If one variant yields consistent captures and the other yields noise, that is
the primary evidence for the Manchester question.

### 3.1 Enable campaign mode

Via the REST API:

```
POST /api/config
{
  "radio": {
    "prios_capture_campaign": true,
    "prios_manchester_enabled": false
  }
}
```

Or via the web UI: **Settings → Radio → PRIOS capture campaign**.

On next boot (or after applying config) the gateway logs:

```
PRIOS capture campaign ACTIVE — radio locked to WMbusPriosR3 (variant=manchester_off, T-mode suspended)
```

**Important:** Normal T-mode meter reception stops while campaign mode is
active.  Re-enable it by setting `prios_capture_campaign = false`.

### 3.2 Switch between Variant A and Variant B

Update the config while campaign mode is active:

```
POST /api/config
{
  "radio": {
    "prios_manchester_enabled": true
  }
}
```

The new variant takes effect on the next radio task cycle.  Run each variant
for the same duration and collect ≥ 10 captures per variant for a fair
comparison.

### 3.3 Place the gateway near the meter

Place the gateway within 1–2 m of the PRIOS meter and wait.  The capture
service stores the last 8 bounded prefixes (32 bytes each) in a ring buffer.
Each capture entry records the variant that was active when it was taken.

### 3.4 Check capture status

```
GET /api/diagnostics/prios
```

Response includes campaign state, active variant, per-capture variant tags,
and running counters:

```json
{
  "mode": "capture",
  "campaign_active": true,
  "variant": "manchester_off",
  "profile": "WMbusPriosR3",
  "decoding": false,
  "total_captures": 12,
  "total_evicted": 4,
  "recent_captures": [
    {
      "seq": 9,
      "timestamp_ms": 12345678,
      "rssi_dbm": -72,
      "lqi": 90,
      "bytes_captured": 32,
      "variant": "manchester_off",
      "prefix_hex": "A1B2C3..."
    }
  ]
}
```

The **Diagnostics** page in the web UI shows the same information with
`[A]`/`[B]` variant tags on each capture row.

Aim for at least 5 clean captures per variant (RSSI > −100 dBm) before
exporting.

### 3.5 Export the fixture

```
GET /api/diagnostics/prios/export
```

Or click **Download fixture export (JSON)** in the Diagnostics → PRIOS card
(the button appears when at least one capture is present).

Response:

```json
{
  "count": 6,
  "frames": [
    {
      "length": 32,
      "rssi_dbm": -72,
      "lqi": 90,
      "radio_crc_ok": false,
      "radio_crc_available": true,
      "timestamp_ms": 12345678,
      "variant": "manchester_off",
      "hex": "A1B2C3..."
    }
  ]
}
```

Save each variant's export to a separate file — e.g.
`fixture_variant_a.json` and `fixture_variant_b.json`.

### 3.6 Recommended capture workflow

```
1. Set campaign=true, manchester=false (Variant A)
2. Wait for ≥ 10 captures (watch total_captures in /api/diagnostics/prios)
3. Export → save as fixture_variant_a.json
4. Clear the ring buffer:  POST /api/config (no change needed, just reboot)
5. Set manchester=true (Variant B)
6. Wait for ≥ 10 captures
7. Export → save as fixture_variant_b.json
8. Disable campaign mode, restore normal operation
```

---

## 4. Creating a Host-Test Fixture

Create a file under `tests/host/fixtures/` (or inline in a test file).
Keep Variant A and Variant B in separate suites so the analyzer can compare
them independently.

```cpp
#include "wmbus_prios_rx/prios_fixture.hpp"
using namespace wmbus_prios_rx;

// --- Variant A captures (manchester_off) ---
// Paste hex from fixture_variant_a.json
static constexpr PriosFixtureFrame kVariantA[] = {
    {
        .bytes     = {0xA1, 0xB2, 0xC3, /* ... */},
        .length    = 32,
        .rssi_dbm  = -72,
        .lqi       = 90,
        .radio_crc_ok        = false,
        .radio_crc_available = true,
        .timestamp_ms        = 12345678,
        .label     = "r3_varA_cap1",
    },
    // ...
};

// --- Variant B captures (manchester_on) ---
static constexpr PriosFixtureFrame kVariantB[] = {
    /* ... */
};
```

Or build suites at runtime using `PriosFixtureSuite`:

```cpp
PriosFixtureSuite suite{};
std::strncpy(suite.name, "r3_varA_2026-04", sizeof(suite.name) - 1);
suite.append(PriosFixtureFrame::from_record(live_record, "cap1"));
```

---

## 5. Running the Analyzer

Run the analyzer separately on Variant A and Variant B to compare results.

```cpp
#include "wmbus_prios_rx/prios_analyzer.hpp"
using namespace wmbus_prios_rx;

auto analyse = [](const char* label,
                  const PriosFixtureFrame* suite, size_t count) {
    // 1. Vote on per-byte-position stability.
    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(suite, count, votes);

    // 2. Find the common prefix length at ≥75% agreement.
    const size_t cpl = PriosAnalyzer::common_prefix_length(
        votes, PriosFixtureFrame::kMaxBytes, 75);
    (void)cpl;

    // 3. Compute the length histogram.
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(suite, count, hist);

    // 4. Assess overall readiness.
    const PriosAnalyzer::Readiness r =
        PriosAnalyzer::assess_readiness(suite, count, hist, votes);

    // 5. Print a human-readable report.
    char report[4096]{};
    PriosAnalyzer::format_report(report, sizeof(report),
                                  suite, count, votes, hist, r);
    std::printf("=== %s ===\n%s\n", label, report);
};

analyse("Variant A (manchester_off)", kVariantA, kVariantACount);
analyse("Variant B (manchester_on)",  kVariantB, kVariantBCount);
```

Example report output for a working variant:

```
=== Variant A (manchester_off) ===
=== PRIOS R3 Fixture Analysis ===
Frames: 10
Lengths: min=32 max=32 modal=32 (10/10, 100%)
Stable prefix (75% agreement): 6 bytes
Prefix bytes: A1(100%) B2(100%) C3(100%) D4(100%) E5(100%) F6(100%)
Per-frame summary:
  [0] len=32  rssi=-72  lqi=90   A1B2C3D4E5F6...(26)
  ...
Decoder readiness:
  enough_captures    : YES (10 >= 5)
  stable_prefix      : YES (6 >= 4 bytes)
  consistent_lengths : YES (100% >= 60%)
  good_rssi          : YES (best=-72dBm > -100dBm)
  READY FOR DECODER  : YES
```

And for a non-working variant (noise or wrong Manchester setting):

```
=== Variant B (manchester_on) ===
Frames: 10
Lengths: min=14 max=32 modal=17 (3/10, 30%)
Stable prefix (75% agreement): 0 bytes
Decoder readiness:
  enough_captures    : YES (10 >= 5)
  stable_prefix      : NO  (0 < 4 bytes)
  consistent_lengths : NO  (30% < 60%)
  good_rssi          : YES (best=-75dBm > -100dBm)
  READY FOR DECODER  : NO
```

A stable prefix in one variant and noise in the other is strong evidence for
the correct Manchester setting.

---

## 6. Decoder Readiness Criteria

All four criteria must be true for `Readiness::ready_for_decoder`:

| Criterion              | Threshold                          | What it tells you                         |
|------------------------|------------------------------------|-------------------------------------------|
| `enough_captures`      | ≥ 5 frames                         | Statistical basis is sufficient           |
| `stable_prefix`        | ≥ 4 bytes at ≥ 75% agreement       | Sync / preamble pattern is identifiable   |
| `consistent_lengths`   | ≥ 60% of frames share modal length | Frame length is predictable               |
| `good_rssi`            | Best RSSI > −100 dBm               | Signal is not pure noise                  |

Thresholds are compile-time constants in `PriosAnalyzer` and can be tightened
once the protocol is better understood.

### Variant comparison decision table

After running the analyzer on both variants, use this table:

| Variant A (Manchester OFF) | Variant B (Manchester ON) | Conclusion |
|----------------------------|---------------------------|------------|
| READY | NOT READY | Manchester is OFF → use Variant A |
| NOT READY | READY | Manchester is ON → use Variant B |
| READY | READY | Both decode — check prefix bytes; one is likely garbled doubles |
| NOT READY | NOT READY | Wrong frequency, baud rate, or sync word — more investigation needed |

Once the correct variant is identified, update `cc1101_profile_prios_r3.hpp`
to set `MDMCFG2` to the confirmed value and commit the fixture files.

---

## 7. What to Do When Ready

When `ready_for_decoder == true`:

1. Identify the sync word from the stable prefix bytes.
2. Update `cc1101_profile_prios_r3.hpp` with the correct sync word and SYNC_MODE.
3. Write a `PriosFramer` (analogous to `WMbusTmodeFramer`) that uses the stable
   prefix to locate frame boundaries.
4. Write a host test that replays the fixture through the framer and asserts
   frame boundaries, length fields, and any known invariants.
5. Only after framing works reliably, begin decoding header fields.

Keep the fixture files as regression data — they are the ground truth for any
future decoder changes.

---

## 8. Key Files

| File | Purpose |
|------|---------|
| `components/config_store/include/config_store/config_models.hpp` | `prios_capture_campaign` + `prios_manchester_enabled` config fields |
| `components/wmbus_prios_rx/include/wmbus_prios_rx/prios_capture_service.hpp` | Live capture ring buffer (`manchester_enabled` per record) |
| `components/wmbus_prios_rx/include/wmbus_prios_rx/prios_fixture.hpp` | Bounded fixture frame / suite types |
| `components/wmbus_prios_rx/include/wmbus_prios_rx/prios_analyzer.hpp` | Offline analysis API |
| `components/wmbus_prios_rx/src/prios_analyzer.cpp` | Analysis implementation |
| `components/radio_cc1101/include/radio_cc1101/cc1101_profile_prios_r3.hpp` | CC1101 register configs: Variant A (`kPriosR3Config`) and Variant B (`kPriosR3ConfigManchesterOn`) |
| `components/app_core/src/runtime_tasks.cpp` | Campaign mode activation and CC1101 profile switching |
| `components/api_handlers/src/prios_handlers.cpp` | `/api/diagnostics/prios` and `/api/diagnostics/prios/export` |
| `tests/host/test_prios_capture.cpp` | Unit tests for capture driver and service (including variant field) |
| `tests/host/test_prios_framer_readiness.cpp` | Machine-readable checklist of blocked framer facts |
| `tests/host/test_prios_fixture_analyzer.cpp` | Unit tests for fixture + analyzer |
