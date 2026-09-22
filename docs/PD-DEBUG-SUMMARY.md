# PD output collapse — short summary

*2026-09-02, third revision. Details: [PD-DEBUG-REFERENCE.md](PD-DEBUG-REFERENCE.md) — start at §0,
which supersedes the first round's conclusions.*

## The symptom

Output spikes to ~42 V, immediately collapses, and never comes back. On the worst port of one PSU,
a constant reboot loop instead. On a powerbank, sits at 5 V with no error and resets every ~10 s in a loop.

## What is actually happening

There are **five independent faults**, and the earlier rounds conflated them.

### Fault 1 — the firmware cannot accept any contract above 6.6 V (software)

Register `0x11` is documented as VOUT at 80 mV/LSB. **On this board it does not measure the PD
rail.** It reads 4.96 V while a 20 V contract is live and the INA238 reads 20.12 V on the same rail,
and it reads **4.7 V** from a 5 V-only source. That 4.7 V is dropout — the signature of a regulated
5 V rail, not of VOUT, which would read ~5.0 V in both cases.

Every threshold built on it was therefore a constant:

| Site | Test | Actual behaviour |
|---|---|---|
| `voutArrived` | VOUT ≥ 0.75 × contract | **false above 6.6 V** → can never accept |
| Ready liveness | VOUT < 0.75 × contract | **always true** → would tear down instantly |
| `isOutputEnabled()` | VOUT ≥ 1 V | always true |
| `isSourcePhysicallyPresent()` | VOUT ≥ 3 V | always true → **detach could never be detected at all** |

So it looped `Requesting` → 2 s timeout → re-request, forever, at the 2.4 s period in the captured
log. Not PSU-specific: unconditional, on every supply and cable. The source was granting 20 V the
whole time (`msgrlt=1`, `VREQ=20.00V`, `ina=20.12V`).

### Fault 2 — a 5 V peak-load brownout (hardware)

The RotoPD Pro's always-on 5 V output is rated **0.4 A continuous / 1 A peak**, and feeds the ESP,
which makes 3V3 and feeds it *back* to the PD board's I²C logic. Constant draw is only ~0.5 W, so
this is a **peak**, not an overload.

Three pieces of hardware evidence:

- **Warm swap:** cold-start into the bad port loops; arriving from a working port within seconds
  works; waiting longer breaks it again. The board fully powers down during the swap, so the only
  thing carried over is capacitor charge.
- **A 100 µF cap held to +5 V makes it reliable.**
- The screen paints partially, stopping at a different point each time, and the frame persists a few
  hundred ms — the ST7789 holding its framebuffer while the ESP is gone.

100 µF over 1 V of sag is ~100 µs at 1 A, so the transient is **short and sharp** — ESP32 boot
bursts and display init. This **rules out fan rotor stall**, which would need tens of thousands
of µF.

### Fault 3 — the INA238 hardware circuit breaker trips on inrush (hardware)

On the RotoPD Pro the INA238's physical **ALERT** pin is wired to the VOUT MOSFET gate circuitry as
a fast hardware circuit breaker. The firmware configured it with a hard 5.0 A threshold
(`setOverCurrentLimit(5000)`) and latched alerting — **zero headroom on a 100 W (20 V @ 5 A)
contract.**

When the 20 V contract is negotiated, VBUS ramps 5 V → 20 V and charges the boost converter's
input/output capacitors. The inrush spike ($I = C\frac{dV}{dt}$) can briefly exceed 5.0 A, the
INA238 asserts ALERT and latches it, the VOUT MOSFET opens, and the boost input drops to 0 V. The
boost output then bleeds down through the sense divider (the pure-RC decay in the log), and
`CurrentControl` calls `safe()` when it reaches 0 V.

Why it is PSU-specific: different chargers/ports have different VBUS slew rates, so only the faster
ones trip the 5.0 A threshold. Why warm-swap works: the boost caps stay charged, so re-applying
20 V causes negligible inrush. Why no error showed: the AP33772S never tripped (`status=0x02`,
`sys=0x12` force-ON), and the firmware never read the INA238 `DIAG_ALERT` register, so the latch
was never cleared nor reported.

### Fault 4 — Premature attach entry & hard resets interrupting AP33772S startup (software)

When connected to a powerbank, VBUS rises to 5 V. The firmware entered `AttachSettle` prematurely at
$t = 0.8\text{ s}$ because `isSourcePhysicallyPresent()` returned true on `rail >= 3.0 V`, even
though the AP33772S had not yet completed its power-on / moisture check (`opmode=0x00`, `status=0x00`).
Per datasheet §7.5, the AP33772S moisture/attach test takes up to **2200 ms** before `STARTED=1` is
asserted.

Because the firmware entered `AttachSettle` prematurely with a 1.2 s timeout, it issued `hardReset()`
at $t = 2.0\text{ s}$, $3.2\text{ s}$, and $4.4\text{ s}$ — repeatedly aborting the AP33772S's CC
handshake in mid-sequence! Only after the 3 reset attempts stopped at 5.7 s did the chip finally
reach `STARTED=1` (at 5.845 s). But by then, the powerbank's internal 6-second low-load auto-shutdown
timer expired due to 0 mA load, shutting down VBUS and looping forever.

**Fixed and verified in firmware:** `Disconnected` now waits for `(status & STARTED_MSK) || (opmode & 0x03) != 0` before
entering `AttachSettle` or writing config. The 1-second-interval log confirms the fix behaves exactly as designed:
no premature attach, no firmware-issued hard resets, config written 3 ms after STARTED.

**But it did not fix the powerbank** — see Fault 5. The 1 s log showed `STARTED` still arriving at **5.75 s**
(not ~2.2 s), with only *one* STARTED (STATUS is polled every 100 ms, so a chip restart cannot hide), and the chip
latching `LGCYMOD` (legacy non-PD) at 8.8 s: it never received/acknowledged a single Source_Capabilities message
(no NEWPDO ever). No contract → ~100 mA load → powerbank auto-off → reboot → deterministic loop.

### Fault 5 — the AP33772S is deaf to caps for ~5.75 s; the powerbank gives up first (hardware + source timing)

**New hardware fact (from the RotoPD Pro V1.2 KiCad schematic):** the AP33772S's **DP and DN pins are
unconnected** on this board — DN has an explicit `no_connect` marker, DP is simply unwired. The USB-C
connector's D+/D− go through the ESD clamp (D4, TPD4E05U06) to nets `USB_P`/`USB_N` that do **not** reach
the AP33772S. So the moisture-detection feature is not wired up at all, and the earlier "powerbank's D−
circuitry trips the DN check" theory is **refuted** — the D− line never reaches the chip.

What remains is a timing problem, proven by the 1 s log:

- `STARTED` arrives at **5.75 s** (not ~2.2 s), with exactly one startup (STATUS is polled every 100 ms,
  so a chip restart cannot hide). The chip is simply not ready to ACK Source_Capabilities until then.
- **No NEWPDO ever** — the powerbank's caps are never acknowledged. The powerbank gives up on PD before
  the chip is ready (it works fine with the HUSB238, which ACKs caps instantly in hardware).
- `LGCYMOD` latches at 8.8 s (§6.7: Non-PD mode after PD negotiation fails) → no contract → ~100 mA load
  → powerbank auto-off at ~10 s → reboot → deterministic loop.
- Wall chargers work because they keep re-sending caps until the chip is ready (~5.7 s); the powerbank
  does not.

**Firmware mitigation (implemented):**
1. **Aggressive re-attach:** after `STARTED`, if no PDOs within **1 s** (or the chip latches `LGCYMOD`),
   the firmware sends `hardReset()` to force the source to re-attach and re-broadcast capabilities —
   which the now-ready chip can ACK. Bounded (5×, 3 s apart) so a genuinely dumb 5 V source is left alone.
2. **Keep-alive load:** while a source is attached but no contract exists and retries are active, the fan
   runs at 100 % to draw extra current from the 5 V rail, stopping the powerbank's low-load auto-off from
   cutting power before the retries can succeed.
3. **OPMODE transition log:** every `CCFLIP/PDMOD/LGCYMOD` change is timestamped, so the next log shows
   exactly when the chip classifies the source.

**What the next log must show to confirm the mechanism:** the `[PDDIAG]` lines (1 s interval) — specifically
when `opmode` first shows `CCFLIP`. If CCFLIP appears at ~1 s, Rp was present early and the 5.75 s is the
chip's own slow startup (moisture check with floating DN, or chip-internal). If CCFLIP appears at ~5.7 s,
the powerbank presents Rp late. Either way the re-attach retry is the correct lever; the answer decides
whether a hardware fix (e.g. tying DN to a defined impedance) is worth pursuing.

## What the first round got wrong

| Claim | Status |
|---|---|
| "The 20 V contract collapses back to vSafe5V" | **Refuted.** It never collapsed; the firmware never accepted it |
| "The MCP4018 mid-scale power-up causes the 42 V spike" | **Refuted for cold start.** `VSELMIN` defaults to 5 V, so VOUT goes live at 5 V before the ESP boots, and the boost has an ~8.5 V floor. Real only when the ESP browns out *while 20 V is already live* — an amplifier, never the initiator |
| "5 V regulator thermal limiting at 28 V" | **Refuted.** Not a linear part on a 48 V-capable board, and board temperature stays flat |
| "Bootstrap deadlock in `CurrentControl`" | **Refuted** by the owner — the digipot never reads 0 Ω |

## What I changed

| Area | Change |
|---|---|
| **Rail sensing** | New `PDManager::setMeasuredRailVoltage()`, fed from the INA238. Replaces register `0x11` in every decision |
| **Fail-open** | A rail reading under 3 V or non-finite means "no measurement", and the dependent checks are **skipped, not failed**. A dead sensor must not reject a good contract or tear down a live one |
| **Granted vs requested** | `_voltage`/`_current` re-read from `readVREQ()`/`readIREQ()` at the Ready transition |
| **Budget** | `budgetVoltageV()` falls back to the contract voltage, so a dead INA238 no longer zeroes the power budget (was finding C4, promoted to critical by the change above) |
| **Task order** | Power monitor now ticks before the PD task — both are 50 ms, so the old order fed a stale reading every pass |
| **Fan** | Held at 0 for the first 2 s, enforced inside `applyFanPwm()` so no caller can bypass it |
| **Bootloop visibility** | Boot count + reset reason on screen, immediately and persistently. The only channel that survives a bootloop. `BROWNOUT` = power, `panic`/`*-wdt` = software crash |
| **INA238 OCP headroom** | `setOverCurrentLimit(5000)` → `6500` mA, so a 5 A contract has margin for ripple/inrush without tripping the hardware breaker |
| **INA238 slow alert** | `INA238_DIAG_SLOW_ALERT` enabled, so OCP compares against the averaged reading, not a sub-ms raw inrush sample |
| **INA238 alert monitoring** | `PowerMonitor::update()` now reads `DIAG_ALERT` every pass, OR-accumulates a sticky latch, and logs any trip (`SHNTOL`/`BUSOL`/`BUSUL`/`TMPOL`) to `DiagLog`. Reading it also clears the hardware latch so the breaker can re-arm |
| **INA238 alert visibility** | `INA FAULT: <text>` shown on the display bottom line, and `INA_FAULT:` appended to the `[PDDIAG]` line |
| **Powerbank Hard Reset** | Attach entry gated on `STARTED`/OPMODE (Fault 4). In `AttachSettle`, a `hardReset()` re-attach is sent as soon as 1 s passes with no PDOs (or `LGCYMOD` latches), bounded 5× with a 3 s backoff — each HRST makes the source re-broadcast caps for the now-ready chip (Fault 5) |
| **Keep-alive load** | While a source is attached but no contract exists and retries are active (`PDManager::isAttachRetrying()`), the fan runs at 100 % so an impatient powerbank does not auto-shut-down on low load before the retries succeed |
| **OPMODE transition log** | Every OPMODE change is logged with a timestamp (`opmode 0x00 -> 0x80 CCFLIP`, …), giving the exact moment the chip classifies the source — the key evidence for Fault 5 |
| **Serial noise** | Removed the stray unformatted `Serial.printf("raw reading: %d")` from `VoltageMonitor::update()` |

**Builds clean:** `pio run` → SUCCESS, RAM 7.1 %, Flash 26.9 %.

## Do this next

1. **Fit the 5 V cap properly.** Low-ESR electrolytic + 10 µF + 100 nF X7R in parallel, short leads.
   ESR matters more than capacitance here — a big general-purpose electrolytic is the wrong part.
2. **Use the new boot counter as an objective test.** Cold-start into the worst port ten times and
   count failures. This is what the earlier fan-at-0 % experiment lacked.
3. **Probe the big VR's input pad while it loops** — the one measurement still open (§0.6).
   `ina=20 V` there means register `0x11` is the 5 V rail and the MOS is fine. `~0 V` means the MOS
   is open despite `sys=0x12` force-ON, and that is the next bug.
4. **Test the powerbank (Fault 5).** Flash the new build and plug in the powerbank. Expected: the fan
   spins up as a keep-alive, `STARTED` at ~5.75 s, then `HRST retry #1..5` every ~3 s until the powerbank
   re-broadcasts caps and the chip ACKs them (NEWPDO → 20 V). Capture the log — the new OPMODE
   transition lines (`opmode 0x00 -> 0x80 CCFLIP`, …) plus the `[PDDIAG]` lines tell us whether the
   5.75 s is the chip's own slow startup (CCFLIP early) or the powerbank presenting Rp late (CCFLIP at
   ~5.7 s). That decides whether a hardware fix (e.g. tying DN to a defined impedance) is worth pursuing.
5. Retest `kMaxRequestVoltageV = 28.0f` on the 28 V port once the cap is in. Untested, not known
   broken.

## Still open

- Why the 42 V output decays as a **pure RC** (τ ≈ 7.9 s through the sense divider → ~35 µF),
  passing straight through 20 V with no inflection. A boost with 20 V on its input cannot do that.
  Also contradicts the expectation that step 0 yields some output — worth a bench check.
- `Tchip` plausibility → the NTC/OTP decision (reference §9.3). Unchanged.
- `shutdown()` leaves `_state == Ready`, so `isReady()` still advertises a live contract afterwards.
- No `Wire.setTimeOut(50)`, no I²C bus recovery — and the 3V3 cross-supply means the PD board's I²C
  logic browns out with the ESP while VBUS stays live, which is a plausible route to a wedged bus.
