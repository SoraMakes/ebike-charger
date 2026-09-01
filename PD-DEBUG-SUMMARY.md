# PD output collapse — short summary

*2026-09-01. Details: [PD-DEBUG-REFERENCE.md](PD-DEBUG-REFERENCE.md) (written for an agent to query).*

## The symptom

On one specific PSU: two boots, 20 V negotiated, 42 V output spikes and immediately collapses,
screen stuck on `(e)`, no shutdown reason. Other PSUs work.

## What is actually happening

**The 20 V contract collapses back to 5 V with the VOUT MOS still closed, and the firmware could
not see it.**

The VR cannot boost from 5 V, so the 42 V dies. Meanwhile:

- `(e)` came from `isOutputEnabled()` testing `readVoltage() >= 1000` mV. Register `0x11` is
  **VOUT** — the VR's *input* — so 5 V passes the test. It could never show `(d)` unless the MOS
  fully opened.
- `Ready` only re-negotiated when the source's **advertised PDO list** changed. The PSU still
  advertises 20 V, so nothing ever fired. The datasheet (§6.3) says the host *must* re-send
  `PD_REQMSG` to recover; nothing did.
- No shutdown reason, because only the thermal and overvoltage latches ever write one. A PD-side
  fault had no path to the screen at all.

Two things made it PSU-specific rather than universal:

1. **The ADC offset.** `+1.736 V` calibration intercept means a dead rail reads 1.74 V, not 0.
   The control law divides by it → demands the full 3 A → the digipot ramps toward maximum
   *exactly while the rail is down*.
2. **The MCP4018 powers up mid-scale (~3.8 A).** Across your second boot the AP33772S is *not*
   reset (it runs off VBUS), so VOUT stays on at 20 V while the digipot commands ~160 W for the
   whole ESP boot. A supply with headroom rides through it; this one folds back.

## What I changed

| Area | Change |
|---|---|
| **Recovery** | Contract-liveness check (VOUT vs contract, 300 ms debounce) → re-sends `PD_REQMSG`, 2 s backoff, 5-attempt bound. Also fires on protection trips and chip restarts. |
| **Diagnostics** | New `src/DiagLog.{h,cpp}`: 40-entry event ring in RTC memory that **survives a brownout**, plus `esp_reset_reason()`. A `[PDDIAG]` line every 2 s that runs *before* every early return. |
| **Truth** | Sticky STATUS latch (the register is read-clear — every poll used to destroy the fault evidence). Screen now shows measured VOUT in volts and `PD FAULT: OCP`. |
| **Timing** | PD task 1 s → 50 ms. Every constant in `PDManager` is sub-second, so at 1 s all of them were true on every call. |
| **Safety** | `i2c_read()` returned success on a short read — a failed CONFIG read made `clearConfig()` write `0x00` and disable **every** protection. |
| **Boot** | Digipot zeroed first thing after `Wire.begin()`; fan no longer 100 % at boot; GPIO13 `INPUT_PULLDOWN`. |
| **`platformio.ini`** | `monitor_dtr = 0` / `monitor_rts = 0` — **without these, opening the serial monitor resets the board and wipes the evidence you came to read.** |

## Do this next

1. **Build and flash.** Then verify the diagnostics are actually in the image:
   `strings .pio/build/ttgo-t-display/firmware.bin | grep -c PDDIAG` → must be ≥ 1.
2. Reproduce on the bad PSU, then attach the PC and read the log. Three lines settle it:

   | You see | It was |
   |---|---|
   | `st=READY contract=20.0V` + `VOUT=5.xx` + `ina=5.x`, no fault bits | contract collapsed to 5 V — confirmed |
   | `VOUT=0.00` + `sticky=…FAULT: OCP` | a chip protection trip instead |
   | `VOUT=20.xx` but `Iout=0.00` while `pot>10` | MOS open, you're reading a charged input cap |

   Plus `boot #2, reset reason: BROWNOUT` and `digipot at boot: step ~64` to confirm the
   brownout/mid-scale leg independently.

## Two decisions that are yours

- **Is an NTC fitted on the RotoPD Pro's OTP pin?** `OTP_EN` and `DR_EN` are armed while
  `TR25..TR100` are never written, which §7.4 forbids. If `Tchip=` in the log is implausible
  (0, 255, or not tracking the fan), clear both. If plausible, call `setNTC()`.
- **PDO selection policy.** Currently "highest usable voltage". The audit suggests "most power"
  (`V × A`), but that moves the operating point on PSUs that already work — e.g. it would pick
  15 V/5 A over 20 V/3 A. Left as-is with a comment at the decision point.

## Caveat

**Nothing in the current tree has been compiled.** Exactly one successful `pio run` exists
(2026-08-31 23:41:47), but *every* changed file — including `DiagLog.h`/`.cpp` themselves — has
been edited since. Braces balance and the reasoning was checked by hand; that is all I can claim.

Highest risk, being the largest and most recent edits: the `PDManager` rework, the
`DisplayManager::update()` signature change (source-breaking for any const-ref caller), the
`AP33772S` library edits, and the `main.cpp` task table.

Also still open: `_voltage`/`_current` are published from the *request*, not from
`readVREQ()`/`readIREQ()` on success, so the screen can still show a contract that was never
granted. The liveness check covers the consequence but not the cause.
