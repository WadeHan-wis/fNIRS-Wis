# fNIRS BLE Protocol Specification — v4

**Firmware reference version:** v0.1.26
**Protocol version:** 4 (`BLE_PROTOCOL_VERSION = 4`, readable via the VERSION characteristic below)

## Scope

This document describes the BLE GATT interface exposed by the fNIRS device firmware: service/characteristic layout, the measurement configuration format, and the streamed data (batch) frame format.

---

## 1. GATT Service / Characteristics

Base UUID: `xxxxxxxx-1212-efde-1523-785feabcd123`

| UUID (16-bit shorthand) | Name | Properties | Size | Purpose |
|---|---|---|---|---|
| `0x1523` | AS7341_SERVICE | Service | — | Primary service UUID, also advertised for scan filtering |
| `0x1525` | AS7341_CONFIG | Read + Write | 10 bytes | Measurement configuration (gain/integration time/LED duty/cycle/active window) |
| `0x1526` | AS7341_DATA0 | Notify | variable (1 + 14×N bytes) | Batched samples from NIR sensor 1 |
| `0x1527` | AS7341_DATA1 | Notify | variable (1 + 14×N bytes) | Batched samples from NIR sensor 2 |
| `0x1528` | AS7341_VERSION | Read-only | 4 bytes | `fw_version` (u16 LE) + `protocol_version` (u16 LE) |
| `0x1529` | AS7341_SEQ | Notify (declared, inactive) | 4 bytes | Legacy — retained for backward compatibility only; no longer notified since protocol v3 (sequence number is now embedded in each DATA0/DATA1 record instead) |
| `0x152A` | AS7341_DROPPED_COUNT | Read-only | 4 bytes | Cumulative ring-buffer overflow counter (u32 LE) — poll this to distinguish a real data-loss gap from a benign notify delivery miss |

The device advertises the `AS7341_SERVICE` (0x1523) UUID; the full device name (see §4) is carried in the scan response, not in the advertising packet itself.

---

## 2. AS7341_CONFIG (0x1525) — 10 bytes, little-endian

| Offset | Size | Field | Range / Unit | Notes |
|---|---|---|---|---|
| 0 | 1 | `integration_time` | 1–255 (×20ms nominal) | Internally mapped to a fixed ATIME + variable ASTEP |
| 1 | 1 | `location_interval` | seconds | **Not implemented** — accepted and read back, but has no effect on device behavior |
| 2 | 1 | `led1_pwm` (640nm) | 0–100% | |
| 3 | 1 | `led2_pwm` (680nm) | 0–100% | |
| 4 | 1 | `led3_pwm` (980nm) | 0–100% | |
| 5 | 1 | `led4_pwm` | 0–100% | **Ignored** — hardware only has 3 LEDs |
| 6–7 | 2 | `cycle_period_ms` | ms (u16 LE) | Controls how often a measurement is taken |
| 8–9 | 2 | `active_window_ms` | ms (u16 LE) | Clamped to `cycle_period_ms` if larger |

**Power-on defaults** (before any CONFIG write): gain = 256×, integration ≈ 19.7ms, `cycle_period` = `active_window` = one RTC tick (i.e. no gating — free-running at ~10Hz), all three LEDs at 50% duty.

The firmware has no concept of a "session" — it simply retains the last CONFIG value written until power-off. The connecting application is expected to explicitly write its desired settings after connecting; nothing is applied automatically beyond the power-on defaults above.

Configuration can be read back at any time via a normal characteristic read on the same UUID, to confirm applied values.

---

## 3. AS7341_DATA0 / AS7341_DATA1 (0x1526 / 0x1527) — batched sample frames

Each characteristic streams samples from one of the two onboard NIR sensors independently. Both use the same frame format.

### Frame layout

| Field | Size | Description |
|---|---|---|
| `sample_count` | 1 byte | Number of records in this notification (≥ 1) |
| `records[sample_count]` | 14 bytes each | See below |

### Record layout (14 bytes, little-endian)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0–3 | 4 | `timestamp_us` | u32 LE, device-relative microsecond counter. **Wraps every 2³² µs (≈71.58 minutes)** — this is expected, defined-behavior overflow, not a fault. Do not treat a decreasing value as an error by itself; use it only for short-interval timing, or unwrap it if you need an absolute session clock. |
| 4–7 | 4 | `seq_num_and_flag` | u32 LE. **Bit 31** = dark-frame flag (see §5). **Bits 0–30** = monotonic sequence number (wraps at ~2.1 billion — not a practical concern). |
| 8–9 | 2 | `red630` | u16 LE, raw ADC count |
| 10–11 | 2 | `red680` | u16 LE, raw ADC count |
| 12–13 | 2 | `nir` | u16 LE, raw ADC count |

`sample_count` (i.e. the batch size, N) is not fixed — it is computed automatically per connection from the negotiated ATT MTU (up to a maximum of 17 records/frame at MTU 247). If MTU negotiation fails, it falls back to 1 record/frame, using the same frame format (no separate legacy format).

Values are **raw, unprocessed sensor counts** — no calibration, no Beer-Lambert conversion, no baseline subtraction is applied on-device except for the dark-frame mechanism below.

---

## 4. Advertising / Connection Parameters

| Item | Value | Status |
|---|---|---|
| Advertised name | `TedNeuro_v{MAJOR}.{MINOR}.{PATCH}` (e.g. `TedNeuro_v0.1.26`) | Updated every firmware patch release |
| Connection interval | 30ms | Initial value, subject to change after power measurements |
| Peripheral latency | 4 | Initial value |
| Target MTU | 247 (244-byte payload) | Falls back automatically if the central rejects it |
| Tx power | -8dBm (fixed) | **Not yet implemented** |
| Pairing / bonding | Disabled | Intentional for now; may be revisited before production |

---

## 5. Dark-frame subtraction (introduced in v4)

Starting at firmware v0.1.24, roughly **1 in every 10 lit samples** is replaced by an additional **dark frame**: a measurement taken with all LEDs off, using the *same* gain/integration-time settings as the surrounding lit samples. This exists to let the receiving application subtract out ambient light and sensor dark current:

```
corrected_signal = lit_raw - most_recent_dark_raw   (per channel, no scaling needed —
                                                       both use identical gain/integration time)
```

Because the AS7341's raw count is linear with integration time (a pure accumulation, not a multiplicative/gain-dependent transform, given equal gain/integration settings), a straight subtraction is valid with no scaling factor.

Implementation notes for the receiving application:
- Identify dark frames via bit 31 of `seq_num_and_flag` (§3), not by any separate field.
- Dark frames arrive at a lower rate than lit frames — hold the most recent dark value per sensor and reuse it for each subsequent lit sample until a new one arrives.
- Dark frames should **not** be plotted alongside lit frames as-is; they are expected to read near zero/ambient level.
- If ambient light changes rapidly between a dark frame and the following lit frames (e.g. a strong external light source flickering), the subtraction can produce negative results — this is a known, physically-expected limitation of the technique, not a bug.

---

## 6. Known hardware caveats affecting interpretation

- **LED3 wavelength**: specified as 950nm, but the currently deployed prototype board ("PoC v1") is populated with a 980nm LED part instead. Firmware field names and this document refer to it as "950nm" throughout for consistency, but the actual physical wavelength is 980nm on current hardware.
- **"NIR" channel**: this is the AS7341's NIR photodiode channel (~910nm peak response), used as the closest available filter to the 950/980nm LED — it is a raw ADC channel, not a derived or named-wavelength-specific measurement.
- **Sensor 2 (DATA1) baseline signal** is consistently much weaker than sensor 1 (DATA0) in internal testing; this has not yet been root-caused (may be optical coupling, may be hardware-specific) and should be treated as an open item, not assumed to be a parsing error.

---

## 7. Protocol version history

| Version | Change |
|---|---|
| v1 | Fixed 8-byte frame, no timestamp/seq_num, included a now-dead LED-index field |
| v2 | Frame expanded to 16 bytes, added `timestamp_us`/`seq_num` directly |
| v3 | Switched to the variable-length batching frame (`[count] + record×N`), removed the dead LED-index field |
| **v4 (current)** | Same frame structure as v3; repurposes bit 31 of `seq_num` as a dark-frame flag (no frame-length change) |

Applications should read `AS7341_VERSION` (0x1528) at connection time and confirm `protocol_version == 4` before assuming this frame layout.
