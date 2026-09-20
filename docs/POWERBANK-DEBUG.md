# Powerbank USB-PD negotiation failure — full debug writeup

*2026-09-02. Companion to [PD-DEBUG-SUMMARY.md](PD-DEBUG-SUMMARY.md) and
[PD-DEBUG-REFERENCE.md](PD-DEBUG-REFERENCE.md). This document is specifically about the
**powerbank** failure mode (a USB-C powerbank that will not negotiate PD with the RotoPD Pro /
AP33772S), and records every hypothesis, experiment, and result so far.*

> **Code baseline for the receiver.** The code handed out with this document is git HEAD
> `2d72480` ("claude changes 1"). It is the state **before any powerbank work**: it contains the
> earlier Faults 1–3 fixes (rail sensing via INA238, INA238 circuit-breaker headroom, etc.) but
> **none** of the powerbank rounds 1–5 — those are all uncommitted. The exact change for each
> round is given in §4 so it can be re-applied if you want to reproduce or continue the work.
> §8 summarises the baseline vs. the uncommitted deltas.

---

## 1. The symptom

When the charger is powered from a USB-C **powerbank**:

- The board powers up (5 V rail present, ESP boots, display works).
- It sits at 5 V, shows **no error**, and never negotiates a PD contract.
- After ~10 s the powerbank auto-shuts-down (low load / no PD), VBUS drops, the ESP reboots,
  and the cycle repeats forever.

The same board negotiates 20 V fine with **wall chargers** (PD PSUs). The same powerbank negotiates
PD fine with an **Adafruit HUSB238** board (a different, simpler PD sink controller).

So: the failure is specific to the **combination** of this powerbank + the AP33772S/RotoPD Pro.

---

## 2. Hardware / system under test

| Element | Detail |
|---|---|
| MCU | ESP32, LilyGO TTGO T-Display (ST7789 240×135) |
| PD sink | AP33772S on a CentyLab **RotoPD Pro** (240 W / 48 V version), I²C `0x52` |
| Regulator | Boost converter, AP33772S VOUT → ~42 V charger output. No enable pin. |
| Current set | MCP4018 digipot `0x2F` in the VR feedback path |
| Input sense | INA238 `0x40`, 5 mΩ shunt (also a hardware circuit breaker via its ALERT pin) |
| Power domains | RotoPD Pro always-on 5 V out → ESP → ESP 3V3 → back into RotoPD Pro's I²C logic. AP33772S runs off VBUS and is **not** reset by ESP brownouts. Anything dropping VBUS reboots the MCU. |
| Source under test | A USB-C powerbank (works with HUSB238, fails with AP33772S) |

---

## 3. The evidence (captured logs)

### 3.1 1-second-interval diagnostic log (round 2, before the early-attach experiment)

```
[DIAG] b1  0.050s boot #1 (power-on)
[DIAG] b1  0.052s digipot at boot: step 63 (~3.74A)
[DIAG] b1  0.796s rail measurement available (5.02V)
[DIAG] b1  2.005s rail ? -> LOW(~5V) (5.02V) contract=0.0
[DIAG] b1  5.752s status=0x01 STARTED
[DIAG] b1  5.755s attach: rail=5.02V opmode=0x80 (STARTED)
[DIAG] b1  7.008s status=0x02 READY
[DIAG] b1  8.796s AttachSettle: legacy 5V non-PD source
       (≈10s) powerbank cuts VBUS -> ESP reboots -> identical cycle
```

### 3.2 The `[PDDIAG]` line (the key data point)

```
[PDDIAG] st=SETTLE contract=0.0V/0.00A pdo=0 | rail=5.05V r11=4.80V VREQ=5.00V IREQ=0.00A
Iout=0.17A Tchip=23C | status=0x01 sticky=0x01 msgrlt=0 opmode=0x80 cfg=0xF0 mask=0x7F
sys=0x12 voutctl=2 | ina=5.05V/0.00A out=0.00V pot=0 | boot#1 power-on
```

With the OPMODE transition log (round 3):

```
[DIAG] b1  0.798s opmode 0xFF -> 0x00
[DIAG] b1  5.803s status=0x01 STARTED
[DIAG] b1  5.804s opmode 0x00 -> 0x80 CCFLIP
[DIAG] b1  5.806s attach: rail=5.05V opmode=0x80 (STARTED)
```

### 3.3 What the evidence proves

1. **VBUS (5 V) is present from ~0.8 s** (rail measurement available), but the chip reports
   **`opmode 0x00` (no source detected) until ~5.8 s**.
2. **`STARTED` and `CCFLIP` (CC2 attach) both appear at ~5.8 s** — the chip does not detect the
   powerbank's CC/Rp until then.
3. **No `NEWPDO` ever fires** — the chip never receives/acknowledges a single Source_Capabilities
   message. `msgrlt=0`, `pdo=0`.
4. **`LGCYMOD` latches at ~8.8 s** — per datasheet §6.7 the chip enters Non-PD (legacy) mode after
   PD negotiation fails.
5. The failure is **deterministic per attach** — the power-cycle loop is itself an endless retry
   that never succeeds.

---

## 4. Everything tried, in order

> **Prior context (not part of the powerbank debugging):** before the powerbank work, the board had
> a separate failure — the output spiked to ~42 V then collapsed on fast-slew wall-charger ports.
> That was root-caused to the **INA238 hardware circuit breaker** tripping on inrush (Fault 3 in
> `PD-DEBUG-SUMMARY.md`) and fixed by raising the OCP limit to 6500 mA, enabling
> `INA238_DIAG_SLOW_ALERT`, and reading/clearing the alert latch every pass. That issue was about
> the **output current / collapse**, not the powerbank, and is documented in the summary/reference.
> The powerbank debugging below starts from the first powerbank-specific attempt.

### Round 1 — first powerbank attempt: hard reset after 1.2 s

**Hypothesis:** the powerbank is a DRP that needs a hard reset to re-discover the sink.

**Change:** `AttachSettle` issued `_pd.hardReset()` after a 1.2 s timeout (up to 3 times).

**Result:** **Failed.** Exact same behavior. The log showed hard resets at 2.0/3.2/4.4 s and
`STARTED` at 5.845 s — the resets were interrupting the chip's own startup.

### Round 2 — gate attach on STARTED, raise reset delay

**Hypothesis:** the firmware was entering `AttachSettle` prematurely (on rail voltage alone at
0.8 s) and its hard resets were aborting the AP33772S's internal startup.

**Changes:**
- `Disconnected` now waits for `(status & STARTED_MSK) || (opmode & 0x03) != 0` before entering
  `AttachSettle` / writing config.
- `kAttachHardResetTimeoutMs` raised to 3000 ms (must exceed the chip's 2200 ms startup).
- `kMaxAttachHardResetAttempts` = 2.

**Exact code (re-apply to reproduce; the receiver's baseline still uses the old
`isSourcePhysicallyPresent()` gating):**

`PDManager.h`:
```cpp
static constexpr uint32_t kAttachHardResetTimeoutMs = 3000;
static constexpr uint8_t  kMaxAttachHardResetAttempts = 2;
```

`PDManager.cpp` — `Disconnected` case (replaces the old
`(status & STARTED_MSK) || isSourcePhysicallyPresent()` gate):
```cpp
const bool chipStarted = (status & STARTED_MSK) != 0;
const bool sourceDetected = (opMode & 0x03) != 0;
if (chipStarted || sourceDetected) {
    applyStartupConfig();
    _state          = PDNegotiationState::AttachSettle;
    _stateEnteredAt = millis();
    _lastPdoPollMs  = 0;
    _attachHardResetAttempts = 0;
    _legacyAnnounced = false;
    _nextRequestAllowedMs = 0;
}
```

**Result:** The fix behaved exactly as designed (no premature attach, no firmware resets, config
written 3 ms after STARTED) — **but the powerbank still failed.** `STARTED` still arrived at
~5.75 s, and the chip latched `LGCYMOD` at 8.8 s. This proved the 5.75 s is **not** caused by our
resets — it's the chip's own behavior with this powerbank.

### Round 3 — OPMODE logging + aggressive re-attach + keep-alive

**New data:** the `[PDDIAG]` line and OPMODE transition log showed `opmode 0x00` until 5.8 s, then
`CCFLIP` + `STARTED` together at 5.8 s. The chip does not detect the source until 5.8 s.

**Hypothesis:** the chip is deaf to caps for ~5.8 s (its startup), the powerbank gives up before
then, and the only lever is to force the source to re-broadcast caps via hard reset — plus keep the
powerbank alive with a fan load so it doesn't auto-off during the retries.

**Changes (uncommitted):**
- `AttachSettle` sends `hardReset()` as soon as 1 s passes with no PDOs after STARTED (or
  `LGCYMOD` latches), bounded 5× with a 3 s backoff.
- Added `ThermalManager::setKeepAlive()` — fan at 100 % while `isAttachRetrying()`.
- Added OPMODE transition logging.

**Exact code (re-apply to reproduce):**

`PDManager.h` constants:
```cpp
static constexpr uint32_t kAttachHardResetTimeoutMs = 1000;
static constexpr uint8_t  kMaxAttachHardResetAttempts = 5;
static constexpr uint32_t kAttachHardResetBackoffMs = 3000;
```

`PDManager.h` members (the receiver's baseline lacks these):
```cpp
uint8_t  _attachHardResetAttempts = 0;
bool     _legacyAnnounced = false;
uint8_t  _lastOpMode = 0xFF;   // last OPMODE read, for transition logging
uint32_t _nextRequestAllowedMs = 0; // earliest millis() another PD_REQMSG/HRST may be sent
```

`PDManager.cpp` — OPMODE transition log, read once per `update()` after the STATUS poll:
```cpp
const uint8_t opMode = _pd.readOpMode();
if (opMode != _lastOpMode) {
    DiagLog::event("opmode 0x%02X -> 0x%02X%s%s%s",
                   _lastOpMode, opMode,
                   (opMode & 0x80) ? " CCFLIP" : "",
                   (opMode & 0x02) ? " PDMOD" : "",
                   (opMode & 0x01) ? " LGCYMOD" : "");
    _lastOpMode = opMode;
}
```

`PDManager.cpp` — `AttachSettle`, after the PDO poll finds nothing:
```cpp
const bool legacyLatched = (opMode & 0x01) != 0;
const bool noCapsTimeout = (now - _stateEnteredAt >= kAttachHardResetTimeoutMs);
if ((legacyLatched || noCapsTimeout) &&
    _attachHardResetAttempts < kMaxAttachHardResetAttempts) {
    ++_attachHardResetAttempts;
    _pd.hardReset();
    _stateEnteredAt = now;
    _lastPdoPollMs  = 0;
    _nextRequestAllowedMs = now + kAttachHardResetBackoffMs;
}
```

`ThermalManager.{h,cpp}`:
```cpp
void setKeepAlive(bool active);          // header
bool _keepAlive = false;                 // member
void ThermalManager::setKeepAlive(bool active) { _keepAlive = active; }
// in update(): if (_keepAlive) fanDuty = kFanFullDuty;
```

`main.cpp` — in `updatePd()` after `pdManager.update()`:
```cpp
thermalManager.setKeepAlive(pdManager.isAttachRetrying());
```

**Result:** **Failed.** "Behaviour is still exactly the same." The retry is futile because each
`hardReset()` restarts the chip, which again takes ~5.8 s to present Rd — the first retry completes
at ~12.6 s, after the powerbank's ~10 s auto-off.

### Round 4 — DN (moisture) hardware theory

**Hypothesis:** the chip's 5.8 s startup is its moisture check on the DN pin, which is **floating**
on the RotoPD Pro (confirmed from the V1.2 KiCad schematic: DN pin 18 is `no_connect`, DP pin 19 is
unwired; the USB-C D+/D− go through the ESD clamp to nets that never reach the chip). The datasheet
says the check measures DN-to-GND impedance; floating DN might fail/retry for ~5.8 s.

**Experiment:** tied the AP33772S **DN pin to GND** through **100 kΩ**, then **10 kΩ**.

**Result:** **Failed.** No change (observed known behavior; no logs captured). This refutes the
moisture-check-as-cause theory — tying DN to GND did not speed up startup.

### Round 5 — early attach (current, untested)

**Hypothesis (user's idea):** don't wait for `STARTED` (5.8 s); the ESP and PD board boot together
from VBUS, so the chip's I²C is up by ~0.8 s. Enter `AttachSettle` at ~1 s and write config early,
so the chip is configured and ready to receive caps the moment it detects the source. Also starts
the keep-alive fan at 1 s instead of 5.8 s.

**Changes (implemented, builds clean, NOT yet tested on hardware):**
- `Disconnected` enters `AttachSettle` when the rail is present **and** ~1 s has passed
  (`kEarlyAttachMs = 1000`), in addition to the STARTED/source triggers. Logs `(early)`.
- The Hard Reset retry in `AttachSettle` is gated on `STARTED` having been seen, so the early entry
  cannot reset the chip mid-startup.

**Exact code (re-apply to reproduce):**

`PDManager.h`:
```cpp
static constexpr uint32_t kEarlyAttachMs = 1000;
```

`PDManager.cpp` — `Disconnected` case:
```cpp
const bool chipStarted = (status & STARTED_MSK) != 0;
const bool sourceDetected = (opMode & 0x03) != 0;
const bool earlyReady = _railValid && (millis() >= kEarlyAttachMs);
if (chipStarted || sourceDetected || earlyReady) {
    applyStartupConfig();
    DiagLog::event("attach: rail=%.2fV opmode=0x%02X%s%s",
                   _railVoltageV, opMode,
                   chipStarted ? " (STARTED)" : "",
                   earlyReady ? " (early)" : "");
    _state          = PDNegotiationState::AttachSettle;
    _stateEnteredAt = millis();
    _lastPdoPollMs  = 0;
    _attachHardResetAttempts = 0;
    _legacyAnnounced = false;
    _nextRequestAllowedMs = 0;
}
```

`PDManager.cpp` — `AttachSettle`, gate the Hard Reset on STARTED having been seen:
```cpp
const bool legacyLatched = (opMode & 0x01) != 0;
const bool startedSeen = (_statusSticky & STARTED_MSK) != 0;
const bool noCapsTimeout = startedSeen &&
    (now - _stateEnteredAt >= kAttachHardResetTimeoutMs);
```

**Status:** awaiting hardware test.

---

## 5. What has been ruled out

| Hypothesis | Result |
|---|---|
| Firmware hard resets aborting chip startup (round 1) | **Refuted** — STARTED is ~5.75 s with and without resets |
| Premature attach entry on rail voltage (round 2) | **Fixed in firmware, but not the cause** — still fails with correct gating |
| Powerbank's D− circuitry tripping the DN moisture check | **Refuted** — DN is `no_connect` on the RotoPD Pro; the D− line never reaches the chip |
| Moisture check with floating DN causing the 5.8 s startup | **Refuted** — tying DN to GND (100 kΩ, 10 kΩ) changed nothing |
| CC1/CC2-specific board fault | **Refuted** — flipping the cable/plug 180° doesn't help |
| Cable / data lines | **Refuted** — several USB-C to C cables tried, no change |
| Powerbank not doing PD at all | **Refuted** — it negotiates fine with the HUSB238 |

---

## 6. Remaining hypotheses

1. **The chip's source detection is genuinely delayed to ~5.8 s** (inherent startup, not moisture),
   and the powerbank's PD window / auto-off (~10 s) is shorter than that. The chip presents Rd late;
   the powerbank never engages PD. → The early-attach experiment (round 5) is the test.
2. **The powerbank presents Rp late (~5.8 s)** and sends caps once, which the chip misses during its
   post-STARTED config window, and the powerbank doesn't retry. → Early config (round 5) may catch
   them.
3. **The powerbank needs a larger load to engage PD** — the keep-alive fan from 1 s (round 5) may
   trigger it.

If round 5 fails, the most likely conclusion is that **this powerbank is incompatible with the
AP33772S's slow startup**, and the practical fix is to use a different sink controller (the HUSB238,
which already works) or a different powerbank.

---

## 7. What would settle it

- **Time the wall charger's `STARTED`.** If a known-good PD PSU also takes ~5.8 s to `STARTED`, the
  chip's startup is inherently slow (and the powerbank is just impatient). If the wall charger
  `STARTED` at ~2.2 s, the 5.8 s is specific to the powerbank (Rp late).
- **Capture a full log after 5.8 s** with the round-5 build to see whether `NEWPDO` appears and
  whether the early config + keep-alive change anything.
- **Scope the CC line** (if available) to see when the powerbank actually presents Rp vs. when the
  chip presents Rd.

---

## 8. Code state: committed baseline vs. uncommitted experiments

### 8.1 The committed baseline (what the receiver has)

Git HEAD `2d72480` ("claude changes 1"). This is the state the receiver works from. It is the
**pre-powerbank** state (Faults 1–3 fixed, no powerbank work):

- `src/PDManager.h` — `kAttachSettleMs = 300` (no `kAttachSettlePollMs`, no hard-reset constants).
- `src/PDManager.cpp` — `Disconnected` enters `AttachSettle` on
  `(status & STARTED_MSK) || isSourcePhysicallyPresent()`; `AttachSettle` polls PDOs every
  `kAttachSettleMs`; **no** hard-reset retry, no OPMODE transition log, no early attach.
- `src/ThermalManager.{h,cpp}` — no `setKeepAlive()`.
- `src/main.cpp` — no keep-alive wiring.
- Builds clean: `pio run` → SUCCESS, RAM 7.1 %, Flash 26.9 %.

### 8.2 The uncommitted deltas (NOT in the receiver's code)

**All five powerbank rounds are uncommitted.** Re-apply from §4 in order if you want to reproduce
or continue them:

| File | Change | Round |
|---|---|---|
| `src/PDManager.cpp` | hard-reset retry after 1.2 s (up to 3×) | 1 |
| `src/PDManager.{h,cpp}` | attach gated on `(opmode & 0x03) != 0`; `kAttachHardResetTimeoutMs=3000`, `kMaxAttachHardResetAttempts=2` | 2 |
| `src/PDManager.{h,cpp}`, `src/ThermalManager.{h,cpp}`, `src/main.cpp` | OPMODE transition log; aggressive re-attach (`1000`/`5`/`3000`); `setKeepAlive()` fan; keep-alive wiring | 3 |
| (hardware) | DN pin to GND via 100 kΩ / 10 kΩ | 4 |
| `src/PDManager.{h,cpp}` | early attach (`kEarlyAttachMs=1000`); HRST gated on STARTED | 5 |

Other uncommitted files (not powerbank-specific): `src/DisplayManager.*`, `src/PowerMonitor.*`,
`src/VoltageMonitor.cpp`, and the `PD-DEBUG-*.md` docs.

### 8.3 Recommended next step for the receiver

1. Start from the committed baseline (§8.1).
2. Re-apply the round-5 early-attach experiment (§4, Round 5) — it is the only untested idea.
3. Test with the powerbank and capture a full log past 5.8 s. Watch for `(early)` at ~1 s, whether
   `NEWPDO` appears, and whether the keep-alive fan keeps the powerbank alive.
4. Also time a known-good wall charger's `STARTED` (see §7) to decide between "chip inherently
   slow" vs. "powerbank presents Rp late".