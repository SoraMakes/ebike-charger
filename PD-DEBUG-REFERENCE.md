# USB-PD charger — PD failure analysis, reference record

Written 2026-09-01 as a queryable record of a debugging investigation into a PSU-specific output
collapse. Intended to be read by an agent answering questions about this work; the human-facing
summary is [PD-DEBUG-SUMMARY.md](PD-DEBUG-SUMMARY.md).

**Confidence conventions used below.** Statements marked *verified* were checked directly against
source, the datasheet text, or build artifacts. Statements marked *inferred* are reasoning that
was not measured on hardware. Statements marked *refuted* were checked and found wrong — they are
kept deliberately, so the same wrong idea is not re-derived.

---

## 1. System under test

| Element | Detail |
|---|---|
| MCU | ESP32, LilyGO TTGO T-Display (ST7789 240×135, `setRotation(1)`) |
| PD sink | AP33772S on a CentyLab **RotoPD Pro** (240 W / 48 V version), I²C `0x52` |
| Regulator | Boost converter, AP33772S VOUT → ~42 V charger output. **No enable pin.** |
| Current set | MCP4018 digipot `0x2F`, in the VR feedback path. Step 0 ≈ 0.14 A (125 Ω), ≈ 0.0572 A/step, 127 steps |
| Input sense | INA238 `0x40` on the RotoPD Pro, 5 mΩ shunt |
| Output sense | Voltage divider 219 k / 4.86 k → GPIO33 (ADC1_CH5), `ADC_0db` |
| Temperature | BMP280 at `0x76` (despite the class being named for an AHT10) |
| Pins | I²C 21/22 · fan PWM 12 · PD INT 13 · ADC 33 · TFT 19/18/5/16/23/4 |

**Power domains** (`pins_and_voltages.md`): the ESP, the fan MOSFET and the PD source share a 5 V
rail; the ESP, digipot, BMP280 and PD trigger share 3V3. **The ESP is powered from the PD source.**
This matters repeatedly: anything that drops VBUS reboots the MCU.

**Unconfirmed hardware facts** (asked but not answered; do not assume):
- Whether the INA238 shunt taps **VBUS (pre-MOS)** or **VOUT (post-MOS)**. Determines what a
  disagreement between `ina=` and register `0x11` means. No RotoPD Pro schematic in this repo.
- Whether an NTC is fitted on the AP33772S OTP pin. Governs the OTP decision in §8.

---

## 2. The reported failure

Verbatim from the user, on **one specific PSU**; other PSUs work:

> connecting to that psu — there seems to be 2 boots (screen flickers); device turns on; it very
> quickly negotiates 20V profile and at the same time the output voltage spikes (turns on), there
> is no 2nd pd loop happening, it happens (nearly) at the same time; immediately the output
> voltage drops again; (e) is always there, no (d); no shutdown reason is shown.

Operational constraint that shaped the whole diagnostic design: **the user can only read serial
after the fact**, by connecting the ESP to a PC while the fault is still latched. Nothing may be
printed only once.

---

## 3. Root cause

### 3.1 Primary mechanism (*inferred, high confidence; not yet confirmed on hardware*)

**The 20 V contract collapses back to vSafe5V with the VOUT MOS still closed, and the firmware was
structurally incapable of seeing it or recovering.**

Chain:

1. VOUT is live at 5 V before the 20 V request — `applyStartupConfig()` writes `SYSTEM = 0x12`
   (VOUTCTL = 2, force ON). Even without it, `VSELMIN` is never written, so it stays at its
   power-on 5000 mV and the chip closes the MOS on the default 5 V contract by itself
   (guide §8.4). The VR's input capacitance is therefore already attached when 5→20 V happens.
2. The request asks for the source's full advertised current (`CURRENT_SEL = fixed.current_max`),
   and `OCPTHR` is never written — so the chip's OCP threshold is 110 % of the PDO's IMAX
   (5.5 A for a 20 V/5 A PDO). **The AP33772S's own OCP is effectively disarmed**; the only
   current limit that trips is the PSU's. This is why the failure is PSU-specific.
3. The load step lands on a marginal supply. The 42 V spikes — which the user observes — and the
   source drops the contract to vSafe5V or hard-resets.
4. Nothing could see step 3. `_voltage` was published from the *request*; `Ready` had no liveness
   check, only a comparison of the source's *advertised* PDO list against the contract; STATUS was
   read into a local and discarded.

### 3.2 Why this and not a chip protection trip

*Verified reasoning.* A protection trip opens the VOUT MOS and **leaves it open** until the host
loads a new `PD_REQMSG` (guide §6.3). VOUT would read ~0 V, and `isOutputEnabled()`'s
`readVoltage() >= 1000` test would have flipped the display to `(d)` within one 500 ms tick. The
user reports `(e)` continuously. A 5 V rail satisfies `>= 1000 mV`; an open MOS does not.

### 3.3 Aggravators (both *inferred*, both independently plausible)

**A. ADC offset drives the current command to maximum.** `calibratedVoltageV = divider × 1.01854 +
1.73636`. That intercept is a straight-line fit taken between 30 V and 41 V; extrapolated to zero
it reports **+1.74 V for a dead rail**. Consequences: the `outputVoltageV <= 0.0f` guard in
`CurrentControl::update()` was unreachable dead code, and `targetAmps = limitWatts /
outputVoltageV` saturates at the 3.0 A ceiling — so the firmware demands maximum current *exactly
when the rail is down or still ramping*. Ramp is `targetLevel/8` = 6 steps per 250 ms, ~3 s to
full (not the "50 % per second" the comment claims).

**B. MCP4018 mid-scale power-up.** The wiper is volatile and powers up at mid-scale (~step 64,
≈ 3.8 A) — confirmed by the user against the datasheet. It shares the 3V3 rail with the ESP. Across
a brownout the **AP33772S is not reset** (VBUS-powered), so VOUT stays on at full contract voltage
while the digipot commands ~160 W for the whole ESP boot — re-creating the load step that caused
the dip. `currentControl.begin()` now runs first after `Wire.begin()`, but firmware cannot reach
the ~200–300 ms bootloader window.

### 3.4 Candidates for the "two boots", ranked

Three independent mechanisms each produce exactly one extra reset, and they can chain:

1. **Brownout** at the ESP32 default ~2.43 V trip, from the VOUT turn-on transient or the fan's
   (previously) 100 % duty inrush. → `esp_reset_reason()` = `ESP_RST_BROWNOUT`.
2. **GPIO12 / MTDI strapping.** GPIO12 is the flash-voltage strapping pin: LOW → 3.3 V,
   HIGH → 1.8 V. It is driven as the fan's LEDC PWM output. A gate pull-up, or residual gate
   charge when the ESP resets while PWM was HIGH, makes the ESP32 run 3.3 V flash at 1.8 V and
   fail to boot. **Invisible in serial — it fails in ROM.** Neither `README.md` nor
   `pins_and_voltages.md` flags it as a strapping pin.
3. **I²C wedge** after a mid-transaction reset, making boot 2 behave differently from boot 1.

### 3.5 Ruled out

| Hypothesis | Why it is dead |
|---|---|
| **"Force-ON at 5 V before the request is the bug"** | *Refuted.* Half the vendor's own examples for this board do exactly that (`RotoPDPro_PDOfix.ino:38` calls `setOutput(1)` in `setup()`, requests a PDO only in `loop()`). It also changes nothing electrically — with `VSELMIN` at default the chip closes the MOS at 5 V anyway (§8.4). Reverting is the ordering the user already labelled *"wip version (pd does not work)"*, and it hot-plugs 20 V onto discharged caps. |
| Thermal shutdown | `ThermalManager` → `requestShutdown()` always writes a reason string that the display renders. "No reason displayed" excludes it. Call sites *verified*: only `VoltageMonitor.cpp:68` and `ThermalManager.cpp:114`. |
| Overvoltage latch | Same argument — reason string `"overvoltage"` would have shown. |
| Wedged I²C bus | A dead bus makes `readVoltage()` return 0 → `(d)`. The user sees `(e)`, so the bus was answering. |
| `currentControl.safe()` collapsing the rail | *Verified impossible.* `safe()` writes digipot step 0 ≈ 0.14 A — a minimum-current setpoint, not an off switch. `_powerLevel = -1` and `= 0` write the identical byte. There is no VR enable. |
| DR (de-rating) | Applies only when the requested PDO is a PPS APDO; this firmware requests fixed PDOs exclusively. |

### 3.6 A near-miss worth recording

At HEAD the `STOP:` line was drawn at **y=145 on a 135-pixel-tall panel** — never visible. Had the
user been running that build, "no shutdown reason displayed" would have proven nothing and the
thermal/overvoltage exclusions in §3.5 would collapse. They do not, because the user's own
uncommitted diff moved that line to y=120 in the same edit that added `(e)`/`(d)`. Since they see
`(e)`, they run the y=120 build. *Verified from the diff.*

---

## 4. AP33772S datasheet quick-reference

All *verified* against `AP33772S-Sink-Controller-EVB-User-Guide.txt`. This section exists so the
3800-line guide does not have to be re-read.

### 4.1 Registers

| Reg | Addr | W | Attr | Power-on | Meaning |
|---|---|---|---|---|---|
| STATUS | 0x01 | 1 | **RC** | 00h | b0 STARTED, b1 READY, b2 NEWPDO, b3 UVP, b4 OVP, b5 OCP, b6 OTP |
| MASK | 0x02 | 1 | RW | 03h | Gates the **INT pin only**, not the STATUS bits |
| OPMODE | 0x03 | 1 | RO | — | b0 LGCYMOD (legacy), b1 PDMOD (PD source). Attach state, *not* MOS state |
| CONFIG | 0x04 | 1 | RW | **F8h** | b3 UVP_EN, b4 OVP_EN, b5 OCP_EN, b6 OTP_EN, b7 DR_EN — all enabled by default |
| PDCONFIG | 0x05 | 1 | RW | 03h | b1 PPS_AVS_EN, b0 EPR_MODE_EN — both enabled; never touched by this firmware |
| SYSTEM | 0x06 | 1 | RO/RW | 10h | b1:0 **VOUTCTL** 0=Auto 1=force OFF 2=force ON; b5:4 CMDVER (RO) |
| TR25…TR100 | 0x0C–0x0F | 2 | RW | 2710h/1041h/0788h/03CEh | NTC resistance table |
| **VOLTAGE** | **0x11** | 2 | RO | — | **VOUT** voltage (post-MOS), LSB 80 mV. *Not* VBUS |
| CURRENT | 0x12 | **1** | RO | — | VOUT current, LSB 24 mA |
| TEMP | 0x13 | 1 | RO | 19h | °C — invalid until TR25…TR100 are written |
| VREQ | 0x14 | 2 | RO | 0000h | Last requested voltage, LSB 50 mV. **Only updated on success** → sticky |
| IREQ | 0x15 | 2 | RO | 0000h | Last requested current, LSB 10 mA. Same stickiness |
| VSELMIN | 0x16 | 1 | RW | 19h = 5000 mV | MOS closes when VREQ ≥ VSELMIN and READY=1 |
| UVPTHR | 0x17 | 1 | RW | 01h = 80 % | % of VREQ |
| OVPTHR | 0x18 | 1 | RW | 19h = 2000 mV | Offset above VREQ |
| OCPTHR | 0x19 | 1 | RW | **00h** | 0 ⇒ threshold = 110 % of the selected PDO's IMAX |
| OTPTHR / DRTHR | 0x1A / 0x1B | 1 | RW | 78h = 120 °C | |
| SRCPDO | 0x20 | 26 | RO | — | 13 × 2 bytes. Guide is self-contradictory on 26 vs 28; 26 is right |
| PD_REQMSG | 0x31 | 2 | **WO** | — | b15:12 PDO_INDEX, b11:8 CURRENT_SEL, b7:0 VOLTAGE_SEL |
| PD_CMDMSG | 0x32 | 1 | **WO** | — | b0 = HRST |
| PD_MSGRLT | 0x33 | 1 | RO | 00h | b2:0 RESPONSE. **Not RC — holds the last result indefinitely** |

### 4.2 SRCPDO entry layout

Bits: `voltage_max` 7:0 · `peak_current`/`voltage_min` 9:8 · `current_max` 13:10 · `type` 14 ·
**`detect` 15**. SPR slots 1–7 use 100 mV/LSB, EPR slots 8–13 use 200 mV/LSB. `CURRENT_MAX` is a
4-bit bucket: code 0 = "0.00–1.24 A (less than)", codes 1–14 = `1.25 + (code−1)×0.25` A,
code 15 = "5.00 A or more". `PD_REQMSG`'s `PDO_INDEX` is valid **only for a detected PDO**.

*Verified:* the C bitfield union in `AP33772S.h` lays out correctly on little-endian Xtensa GCC.
A 20 V/5 A detected fixed PDO (word `0xBCC8`) round-trips exactly. `RDO_DATA_T` with
`PDO_INDEX=3, CURRENT_SEL=8, VOLTAGE_SEL=0` produces `0x3800` — byte-for-byte the datasheet's own
worked example in §8.3.

### 4.3 Behavioural rules that matter here

- **§6.3** — "When overvoltage, undervoltage, overcurrent, or overtemperature protection occurs,
  the associated VOUT MOS switches are turned off… **The host MCU will need to load a new
  PD_REQMSG** to start the PD negotiation process, resuming quick-charging operation."
  *Nothing recovers by itself.*
- **§7.1–7.4** — all protections use a **30 ms debounce**. OVP threshold = VREQ + 2000 mV when
  that is ≤ 20 V, else VREQ × 110 % (so 22 V for a 20 V contract). UVP = 80 % of VREQ.
- **§7.4** — "The host MCU **must** initialize the TR25, TR50, TR75, and TR100 registers before
  reading the TEMP register or enabling OTP and power de-rating functions."
- **§7.5** — moisture detection: bad DN impedance → MOISTURE state, MOS off, source caps not
  acknowledged, re-checked over 2200 ms.
- **§8.2** — after `STATUS.STARTED=1` the host has **~100 ms** to write CONFIG/MASK/SYSTEM before
  the chip's own first negotiation.
- **§8.3 step 4** — "After writing to the PD_REQMSG register, the host MCU should wait for the
  STATUS.READY flag to be set high **and then** read the PD_MSGRLT register." READY alone means
  "ready to receive an I²C request", *not* "the contract was accepted".
- **§8.4** — the canonical way to keep 5 V off a load that needs more is to raise `VSELMIN`.
- **RESPONSE codes**: 0 busy/no response · 1 success · 2 invalid command/argument · 3 not
  supported / rejected · 4 transaction failed (no GoodCRC).

---

## 5. Reading the diagnostics

### 5.1 What is emitted

| Source | Cadence | Content |
|---|---|---|
| `[DIAG] boot #N, reset reason: X` | once at boot | `esp_reset_reason()`, then a replay of the previous boot's retained events |
| `[EVT <t>] …` | on each event | live echo of a ring-buffer entry |
| `[PDDIAG] …` | 2 s, unconditional | full register + system snapshot (below) |
| `[DIAG] b<N> <t> …` | 30 s | full ring-buffer re-dump |

`[PDDIAG]` fields: `st` (DISC/SETTLE/REQ/READY) · `contract` V/A · `pdo` index · `VOUT` (reg 0x11)
· `VREQ` · `IREQ` · `Iout` (reg 0x12) · `Tchip` · `status` (last poll) · `sticky` (OR of every bit
ever seen, RTC-retained) · `msgrlt` · `opmode` · `cfg` · `mask` · `sys` · `voutctl` · `ina` V/A ·
`out` (42 V ADC) · `pot` (digipot step) · `boot#` · reset reason.

`logDiagnostics()` deliberately **never reads STATUS** — that register is read-clear and belongs to
`update()`. It prints the sticky copy instead.

### 5.2 Decision table

| Log signature | Confirms | Do next |
|---|---|---|
| `st=READY contract=20.0V` + `VOUT=5.xx` + `ina=5.x` + no fault bits | **Primary hypothesis: contract collapsed to vSafe5V, MOS still closed.** VREQ will read 20.00 (stale — only written on success) | Confirmed. Consider staged voltage stepping and a lower `CURRENT_SEL`. Every other hypothesis is dead |
| `VOUT=0.00` + `sticky` with `0x20`/`0x10`/`0x40` | Chip protection trip (OCP/OVP/OTP). The `CHIP FAULT` event gives the timestamp | Set `OCPTHR` explicitly. If OTP, check `Tchip` plausibility first (§8) |
| `VOUT=0.00` + no fault bit + `voutctl=1` | `setOutput(0)` was called — false detach or latched `shutdown()` | Cross-check the `DETACH from state N` event; raise `kAbsentConfirmMs` |
| `boot#2 BROWNOUT` + `digipot at boot: step ~64` | Brownout leg **and** the MCP4018 mid-scale power-up, measured on this board | Hardware: bulk capacitance on 3V3/5 V, or a separate ESP supply |
| `Tchip` implausible (0, 255, or not tracking the fan) | No NTC, or TR25…TR100 uninitialised. OTP is armed against a fabricated value | Clear `OTP_EN` and `DR_EN` |
| `opmode=0x00 cfg=0x00 mask=0x00 sys=0x00` | **I²C failure, not chip state** — `SYSTEM` can never legitimately read 0x00 (CMDVER is hardwired) | Add `Wire.setTimeOut(50)` (capital O) and a 9-SCL-pulse bus recovery |
| `cfg=0xF8` (UVP back on) or `mask=0x03` or `voutctl=0` while `st=READY` | The AP33772S reset underneath the firmware | Check whether the `CHIP RESTARTED` event fired; the config-drift repair should catch it |
| `msgrlt=3` in a `REQUEST REJECTED` event | The source **rejected** the RDO — most likely `CURRENT_SEL` = PDO max is more than it will grant | Request less current; blacklist that PDO index for the attach |
| `VOUT=20.xx` but `Iout=0.00` while `pot>10` | MOS is open; you are reading a **charged VR input capacitor**. Voltage alone cannot show this | This is why the `readCurrent()` cross-check exists |
| `out=1.7x` with `pot` climbing to ~50 | The 42 V rail is dead but the ADC offset makes it read 1.74 V, so the control law demands full current | Should now be prevented by `kCalibrationValidFromV`; if it still appears, raise the floor |
| `ina=0.00V/0.00A` while `contract=20.0V` | INA238 dead or absent — this silently zeroes the whole power budget | Fall back to the contract voltage for the budget; surface it on the display |

---

## 6. Where the evidence lives

| Artifact | Path |
|---|---|
| Datasheet text (searchable) | `AP33772S-Sink-Controller-EVB-User-Guide.txt` |
| Unmodified upstream driver | `AP33772S-CentyLab/` (incl. 11 examples, 5 RotoPD Pro-specific) |
| ~15 third-party implementations | `references/` |
| Compiled driver | `lib/AP33772S/AP33772S.{h,cpp}` |
| Stale duplicate (**not** compiled) | `lib/AP33772S/AP33772S/` — byte-identical to upstream, untracked *and not gitignored* |
| Audit raw findings + verdicts | `~/.claude/projects/-tmp-test-charger/8f5f2a20-c4d3-4b14-bc70-948f16b3749b/subagents/workflows/wf_1109627d-53f/journal.jsonl` |
| Audit synthesis (66 KB) | `/tmp/claude-1000/-tmp-test-charger/8f5f2a20-c4d3-4b14-bc70-948f16b3749b/tasks/wg7umejn4.output` |

*Verified from build artifacts* (`.d` file, single-member archive, `idedata.json`, `firmware.map`):
PlatformIO compiles **only** `lib/AP33772S/AP33772S.cpp`, and `#include <AP33772S.h>` resolves to
the forked root header. The nested copy is neither compiled nor on the include path — but it is one
`mkdir src` away from becoming a silent ODR hazard, and the only `.gitignore` entry is `.pio`, so
`git add -A` would commit it.

`build_err.txt` is stale (2026-08-03): a PlatformIO core bug parsing the caret ranges in
`lib_deps`, captured through a PowerShell `*>` redirect (hence UTF-16). Resolved by a core update,
not an ini change — the carets are still there. Safe to delete.

---

---

## 7. Complete findings catalogue

**Lens keys:** `SM` = PD state-machine lens · `REF` = reference-projects lens · `APP` = application-layer lens · `DS` = datasheet/register lens · `RC` = root-cause-hypothesis lens · `BLD` = build/wiring lens.

**Line-number caveat:** the working tree moved during the audit. Where lenses disagree the pre-fix/HEAD line is given first and the later-revision line in parentheses. 99 raw findings → 73 distinct records.

---

### 7.A PD negotiation state machine (`src/PDManager.*`)

**A1 · contract-liveness-never-validated** — critical — `src/PDManager.cpp:152-183` (later :275-307)
- Wrong: the Ready state compares the source's *advertised* PDO list against the *cached* contract and never once reads back VREQ/VOUT/INA to check the contract is still alive.
- Impact: a collapse back to vSafe5V with the VOUT MOS still closed leaves the machine in Ready reporting 20 V forever — the established root cause; "no 2nd PD loop" because the 20 V PDO is still being offered.
- Fix: every Ready tick, require `readVREQ()`/measured VOUT to stay within tolerance of `_voltage`, else drop to AttachSettle and re-request.
- Lenses: RC, SM

**A2 · chip-fault-bits-discarded-and-masked** — critical — `src/PDManager.cpp:41` (MASK) + `:71-84` (read) — **VERIFIED**
- Wrong: `setInterruptRegisterMask(STARTED|READY|NEWPDO)` = MASK 0x07 keeps UVP/OVP/OCP/OTP off the INT pin, and the read-clear STATUS byte is read into a function-local that tests only bits 0-2, destroying bits 3-6.
- Impact: no chip-side protection trip can ever reach the screen, serial, or any variable — fully explains "no shutdown reason is displayed".
- Fix: OR `status & 0x78` into a sticky member at one choke point, expose it, re-print it periodically, and write MASK = 0x7F.
- Lenses: SM, REF, APP, DS

**A3 · no-protection-recovery-path** — critical — `src/PDManager.cpp:152-183` — **VERIFIED**
- Wrong: guide §6.3 requires a fresh PD_REQMSG after any protection trip; the Ready state only re-negotiates when the best PDO *index/voltage* changes, which a trip does not alter.
- Impact: after a trip the VOUT MOS stays open and the 42 V rail is dead until the user unplugs, while the firmware still reports a healthy contract.
- Fix: add a bounded Recovering state (backoff ≥1-2 s, attempt cap, then a non-cutting terminal fault state that keeps printing).
- Lenses: REF, RC

**A4 · status-events-not-accumulated** — critical — `src/PDManager.cpp:71-84, 111, 119, 135, 153`
- Wrong: STATUS is read-clear and each `switch` case tests only the one bit it wants; there is no `_pendingStatus |=` accumulator, so STARTED/NEWPDO/READY latched together are consumed by one read and mostly thrown away.
- Impact: `AttachSettle`'s `status & NEWPDO_MSK` can never be true, a READY arriving in the wrong state is lost, and every state transition falls through on timers instead of events.
- Fix: accumulate into a member latch and have each state clear only the bits it consumed.
- Lenses: SM

**A5 · pd-task-1hz-quantization** — critical — `src/main.cpp:137` (`PeriodicTask pdUpdateTask(75+925,…)`) + `src/PDManager.cpp:6-13` — **VERIFIED, fix sound**
- Wrong: `update()` runs at most once per 1000 ms, so `kStatusPollMs=100`, `kAttachSettleMs=300`, `kProfileRecheckMs=500` (and later `kAbsentConfirmMs=500`, `kContractLostMs=300`) are true on 100 % of evaluations and have no effect.
- Impact: nothing in the PD path reacts faster than 1 s against a 30 ms chip debounce and a tens-of-ms negotiation; the ~100 ms post-STARTED config window (guide §8.2) is always missed.
- Fix: run the PD task at 50 ms — but only together with the sticky STATUS latch and PD_REQMSG rate limiting.
- Lenses: SM, APP

**A6 · vout-on-one-second-before-request** — critical — `src/PDManager.cpp:48, 112-125, 135-143`; `src/main.cpp:48, 137` — **VERIFIED (consequence narrowed)**
- Wrong: with a 1 s tick and `_lastRun = 0`, the sequence is t≈1 s force VOUT ON → t≈2 s write PD_REQMSG → t≈3 s Ready, one state per second.
- Impact: the VOUT MOS is closed a full second before the 20 V request, so the 5→20 V VBUS step propagates through a closed MOS into the VR input caps — though the verifier established VOUT is already live at 5 V from attach regardless (VOUTCTL=auto + VSELMIN 5000 mV), so `setOutput(1)` is electrically a no-op.
- Fix: raise the tick rate so the sequencing constants apply; do **not** add a 5 V dwell (see V8).
- Lenses: SM

**A7 · vout-forced-on-before-request** — ~~critical~~ → low — `src/PDManager.cpp:48` (later :73) — **REFUTED**
- Wrong (as claimed): "every reference does the opposite" — false; CentyLab's own RotoPD Pro examples call `setOutput(1)` before the first request, and with VSELMIN at 5000 mV, AUTO closes the MOS at 5 V too.
- Impact: none electrically today; force-ON only matters because it permanently overrides the VSELMIN/READY gate.
- Fix: if and only if VSELMIN is ever raised (A23), switch `applyStartupConfig()` to VOUTCTL=0 in the same commit.
- Lenses: REF

**A8 · contract-published-before-acceptance** — critical — `src/PDManager.cpp:196-203`; consumed at `src/main.cpp:113, 193`, `src/DisplayManager.cpp:58`
- Wrong: `_voltage`/`_current` are assigned from the PDO table the instant the I2C write of PD_REQMSG is ACKed — before any PD outcome — and are never invalidated on timeout (`:144-148`) or in `shutdown()`.
- Impact: a rejected/failed/collapsed contract still drives the digipot power budget, the 8.5 V gate and the display ("I 20V/5.0A" that does not exist).
- Fix: keep requested values separate, publish `_voltage/_current` only on confirmed success from `readVREQ()`/`readIREQ()`, and zero them on timeout and shutdown.
- Lenses: SM, APP, DS, RC

**A9 · ready-bit-accepted-as-proof** — high — `src/PDManager.cpp:135`
- Wrong: `if ((status & READY_MSK) || msgSuccess || voltageMatched)` — READY means "ready to receive I2C requests / a negotiation finished", including one that was rejected, and it may predate our request.
- Impact: a completed-but-rejected negotiation, or the chip's own 5 V negotiation, drives the machine into Ready with a phantom contract.
- Fix: require a *fresh, post-request* READY **and then** `(readMsgResult() & 0x07) == 1`.
- Lenses: SM, DS

**A10 · msgrlt-sticky-false-success** — critical — `src/PDManager.cpp:131`; `lib/AP33772S/AP33772S.cpp:97-103`
- Wrong: PD_MSGRLT (0x33) is RO and holds the *previous* result until the next message completes; the firmware never invalidates it before issuing a request.
- Impact: on re-negotiation, and after an ESP-only reset (the chip is VBUS-powered and survives), `msgSuccess` is true from a prior contract, so Ready is declared on stale data.
- Fix: timestamp the request and only accept an MSGRLT read taken after a fresh READY edge; treat 0 as busy.
- Lenses: SM

**A11 · msgrlt-rejection-codes-ignored** — high — `src/PDManager.cpp:131, 135-148`
- Wrong: only `== 1` is tested (unmasked; RESPONSE is bits 2:0), so 2 (invalid), 3 (rejected) and 4 (no GoodCRC) are indistinguishable from 0 (busy).
- Impact: a PSU that rejects the RDO produces nothing but a generic timeout line and an identical retry — a prime one-PSU-only failure mode.
- Fix: mask with 0x07 and branch: 1 → Ready, 0 → wait, 2/3/4 → log the code and back off to a lower current/PDO.
- Lenses: REF, DS

**A12 · timeout-retries-same-pdo-forever** — critical — `src/PDManager.cpp:144-148, 187-204`
- Wrong: the 2 s timeout returns to AttachSettle, which deterministically recomputes and re-requests the identical PDO, and clears none of `_voltage/_current/_activePdoIndex`.
- Impact: an infinite retry loop against a phantom contract, with VOUT force-ON the whole time; `_activePdoIndex` is dead state that could have excluded the failing index.
- Fix: blacklist the failed index for this attach, clear the cached contract on the timeout path, and fall back to the next-best PDO then 5 V.
- Lenses: SM

**A13 · vreq-stale-voltagematched-false-positive** — critical — `src/PDManager.cpp:132-133`
- Wrong: `voltageMatched` compares `readVREQ()` against `_voltage`, but VREQ is only updated on a *successful* negotiation and `_voltage` was already set to the desired value (A8) — two stale values agreeing.
- Impact: re-requesting the same voltage, or any 5 V-only source, satisfies the match immediately and declares Ready with no new contract.
- Fix: snapshot VREQ before writing PD_REQMSG and require it to *change* to the requested value.
- Lenses: SM

**A14 · startup-config-one-shot** — critical — `src/PDManager.cpp:33-35, 49`; `src/PDManager.h:101`
- Wrong: `_configured` is set once per ESP boot and cleared nowhere (not on disconnect, not in `shutdown()`), so `applyStartupConfig()` returns immediately forever after.
- Impact: after any hard reset, VBUS collapse (the chip is VBUS-powered), re-attach or MOISTURE exit, the chip reverts to CONFIG=0xF8 (UVP_EN back on, against CentyLab's explicit instruction), MASK=0x03, SYSTEM=0x10 (VOUTCTL auto) — a self-sustaining UVP→hard-reset loop the firmware refuses to break.
- Fix: drop the latch; make the config idempotent, re-assert on every STARTED edge, and read CONFIG/MASK/SYSTEM back and repair on mismatch.
- Lenses: SM, REF, DS, RC

**A15 · config-write-misses-100ms-window** — high — `src/PDManager.cpp:21-25, 40-41, 112`
- Wrong: the guide gives ~100 ms after STATUS.STARTED to write configurable registers before the chip's own 1st negotiation; the earliest possible write is the first 1 Hz tick.
- Impact: `clearConfig(UVP_EN)` — the one change the vendor calls mandatory for RotoPD Pro — may never take effect; the chip negotiates its first contract under power-on defaults.
- Fix: write CONFIG/MASK/VSELMIN from `setup()` right after `begin()` (as the vendor example does) and re-assert on every STARTED edge from a fast loop.
- Lenses: SM

**A16 · ready-state-has-no-exit** — critical — `src/PDManager.cpp:152-183` (esp. `:161, 168-169`)
- Wrong: Ready has exactly one outbound transition, guarded by `bestPdo != 0`; an all-zero PDO table (short read, chip POR, hard reset) yields `bestPdo == 0` and is silently equivalent to "nothing changed". No exit exists for lost READY, protection trip, VOUT collapse or timeout.
- Impact: the firmware sits in Ready indefinitely with a stale 20 V contract while the digipot ramps into a possibly-dead rail, with zero serial output. `_stateEnteredAt` is also reused as the recheck timestamp, destroying the Ready age.
- Fix: handle `bestPdo == 0` explicitly (count, then drop to AttachSettle, force VOUT off, log), and keep `_readyEnteredAt` separate from `_lastRecheckAt`.
- Lenses: SM

**A17 · pdo-selection-maximises-voltage-only** — high — `src/PDManager.cpp:206-221`; `src/PDManager.h:67`
- Wrong: `findBestFixedPdo()` is a pure argmax over voltage bounded at 28 V — no current, no power, no DETECT check, no exclusion of a just-rejected index.
- Impact: a 28 V/1 A EPR slot beats a 20 V/5 A SPR slot (3.6× power loss), and 28 V into the VR pushes the 42 V rail toward the 42.5 V trip.
- Fix: score by `voltage × getPdoMaxCurrentA()`, tie-break low, and lower `kMaxRequestVoltageV` to 20 V unless EPR is wanted.
- Lenses: SM

**A18 · disconnect-detection-slow-and-reads-wrong-node** — high — `src/PDManager.cpp:52-60, 90-107`
- Wrong: `_absentCount >= 5` at 1 Hz = 5 s minimum; OPMODE tracks CC attach (not the MOS), and the documented "VBUS ≥ 3.0 V" fallback actually reads register 0x11 = **VOUT** (the driver's doxygen at `AP33772S.cpp:474` mislabels it too).
- Impact: a silent VOUT-off is indistinguishable from normal operation (PDMOD stays 1); conversely five failed OPMODE reads declare a detach on a plugged-in source and force VOUT off.
- Fix: split presence (OPMODE only, with a real I2C-error return) from output health (VOUT vs negotiated voltage, latched); shorten the debounce to a time window; fix both "VBUS" comments.
- Lenses: SM, DS

**A19 · isoutputenabled-cannot-detect-output-loss** — critical — `src/PDManager.cpp:255-261` (later :534-539); rendered at `src/DisplayManager.cpp:63-64` — **VERIFIED**
- Wrong: `readVoltage() >= 1000` reads register 0x11 = VOUT (the VR's *input*, ~5-20 V), not the 42 V output on the same display line; with VOUT force-ON the test is true at 5 V, 9 V and 20 V alike, and the VR's uncleared input caps hold it above 1 V after a MOS opening.
- Impact: the definitive "(e) never (d)" symptom — the user's single strongest diagnostic reads a different node than the one they are asking about and cannot distinguish a 5 V fallback from a live 20 V contract.
- Fix: publish the *number* (`readVoltage()/1000.0f`) plus the latched STATUS fault bits instead of a boolean, sample it at the PD tick, and delete the now-callerless predicate.
- Lenses: SM, REF, APP, DS, RC

**A20 · shutdown-leaves-state-ready** — high — `src/PDManager.cpp:263-271, 62-69, 227-229`
- Wrong: `shutdown()` sets `_shutdownLatched` and forces VOUT off but leaves `_state == Ready` and the cached contract intact; `update()` then early-returns.
- Impact: `isReady()`/`getVoltage()` keep advertising a live 20 V contract with a dead output, `justDetached()` can never fire again, and nothing survives an ESP reset — after which `applyStartupConfig()` silently re-enables VOUT with the reason string gone.
- Fix: set a Shutdown state, zero the contract, make `isReady()` false while latched, and persist the reason in RTC/NVS with a periodic re-print.
- Lenses: SM

**A21 · no-periodic-request-keepalive** — medium — `src/PDManager.cpp:187-204` (called once per attempt)
- Wrong: PD_REQMSG is written once and never re-sent while Ready; ESPHome documents a 1 s keep-alive because *some chargers* drop the contract without one, and the STM32 reference issues the initial request twice for cold plug-in.
- Impact: on such a source the contract silently reverts to 5 V while the firmware still reports 20 V — a PSU-specific mechanism matching the reported failure exactly.
- Fix: re-write the last successful PD_REQMSG on a ~1 s timer in Ready and issue the initial request twice ~300 ms apart.
- Lenses: REF

**A22 · no-staged-voltage-ramp** — medium — `src/PDManager.cpp:118-128, 206-221`
- Wrong: the firmware jumps straight to the highest PDO immediately after attach with VOUT already conducting; references either stage low-voltage-first or defer the initial negotiation.
- Impact: the single 5→20 V step is the largest possible dV/dt into the VR input capacitance.
- Fix: defer the first request until sensors are up, and if inrush is still a problem step 9 V → 15 V → 20 V (with the MOS gated) rather than in one edge.
- Lenses: REF

**A23 · vselmin-left-at-default** — high — `src/PDManager.cpp:32-50` (no `setVSELMIN` call anywhere in `src/`)
- Wrong: the app needs ≥8.5 V (`main.cpp:15`) and the datasheet's §8.4 mechanism for exactly that is VSELMIN; it stays at 5000 mV while VOUTCTL=2 bypasses the gate entirely.
- Impact: the MOS conducts through every 5 V window and every re-negotiation dip, feeding the VR voltages it cannot regulate from; a 5 V fallback never trips MISMATCH.
- Fix: `setVSELMIN(9000)` in `applyStartupConfig()` — and only with VOUTCTL returned to AUTO in the same change (see A7/V2 cautions).
- Lenses: SM, REF

**A24 · ntc-uninitialised-otp-enabled** — high — `src/PDManager.cpp:40` (only UVP_EN cleared); `lib/AP33772S/AP33772S.cpp:444-461` (`setNTC` never called)
- Wrong: guide §7.4 makes TR25/TR50/TR75/TR100 a hard precondition for reading TEMP or enabling OTP/de-rating; CONFIG keeps OTP_EN=DR_EN=1 at OTPTHR/DRTHR = 120 °C against an uncalibrated table.
- Impact: a fabricated temperature can trip OTP after a 30 ms debounce, opening the VOUT MOS with no reportable cause; also a permanent false "OTP" that would drive any auto-recovery loop forever.
- Fix: call `setNTC()` with the board's real thermistor values, or `clearConfig(OTP_EN)` + `clearConfig(DR_EN)` since ThermalManager already owns thermal protection.
- Lenses: SM, REF, DS

**A25 · protection-thresholds-left-at-defaults** — medium — `src/PDManager.cpp:32-50` (no threshold writes anywhere)
- Wrong: UVPTHR/OVPTHR/OCPTHR/OTPTHR/DRTHR are never written; OCPTHR=0 means "110 % of the selected PDO's IMAX" (5.5 A on a 20 V/5 A PDO), i.e. roughly double the real maximum.
- Impact: chip-side OCP protects nothing against VR inrush or a shorted output, while the actual field configuration is unknown to the firmware and unrecoverable post-mortem.
- Fix: write all thresholds explicitly for this board (OCPTHR ≈ the current actually intended), read them back, and dump them in the periodic diagnostic.
- Lenses: REF

**A26 · voltagejumped-set-before-request-succeeds** — low — `src/PDManager.cpp:172-180`
- Wrong: `_voltageJumped = true` is set before `requestBestPdo()` is attempted, with no `else` branch on failure.
- Impact: a failed re-request still reports a voltage jump (causing an unnecessary `currentControl.safe()`) while Ready keeps the old contract.
- Fix: set the flag only inside the successful branch and log the failure.
- Lenses: SM

---

### 7.B AP33772S driver (`lib/AP33772S/*`)

**B1 · i2c-read-returns-true-on-short-read** — critical — `lib/AP33772S/AP33772S.cpp:874-898` (guard `:889`, `return true` `:897`) — **VERIFIED, fix sound**
- Wrong: the buffer is zeroed, and if the data phase returns fewer than `len` bytes the copy loop is skipped and the function still returns `true`; the four read-modify-write helpers (`setMask/clearMask/setConfig/clearConfig`, `:809-858`) discard the return value.
- Impact: `clearConfig(UVP_EN)` computes `0x00 & ~0x08 = 0x00` and writes **CONFIG=0x00**, disabling DR/OTP/OCP/OVP/UVP for the whole session; `refreshPdoTable()` zeroes a good table; `readStatus()`/`readVoltage()` report benign zeros instead of errors.
- Fix: `if (requestFrom(...) != len) return false;`, make the RMW helpers bail out on a failed read, and prefer writing absolute values with read-back over blind RMW.
- Lenses: SM, DS, RC

**B2 · detect-bit-never-checked** — high — `lib/AP33772S/AP33772S.cpp:117-123` (`isFixedPdoAt`), used by `:125-141`
- Wrong: slot validity is inferred from "both bytes non-zero" and `type == 0`; SRC_PDO bit 15 DETECT — the chip's own populated flag, and the datasheet's precondition on PDO_INDEX validity — is decoded in the struct but never read. Every reference driver gates on it.
- Impact: a stale/partial slot is treated as requestable; `findBestFixedPdo()` picks the highest voltage, so a bogus high-voltage slot wins and the request is rejected (invisibly, per A11).
- Fix: `if (!pdo.fixed.detect) return false;` first and drop the zero-byte heuristic.
- Lenses: REF, DS

**B3 · pdo-current-code0-returns-1a** — high — `lib/AP33772S/AP33772S.cpp:133-141` (`:138 if (code == 0) return 1.0f;`)
- Wrong: the function documents itself as a conservative lower bound and is one for every code except 0, whose real bucket is "0.00 A ~ 1.24 A" — lower bound 0.0 A, not 1.0 A.
- Impact: a code-0 PDO makes the firmware budget ~2× the source's rating (`main.cpp:113`), while OCPTHR is pinned to that same small bucket — a textbook one-PSU-only "turns on then immediately drops".
- Fix: return 0.0 f for code 0 and have `findBestFixedPdo()` skip any PDO below a usable minimum.
- Lenses: APP, DS, SM

**B4 · current-sel-copied-from-current-max** — medium — `lib/AP33772S/AP33772S.cpp:111`
- Wrong: `CURRENT_SEL = fixed.current_max` — the two encodings coincide for codes 1-15 but not at 0 (advertised "<1.24 A" vs requested "1.00 A"), and it always requests the PDO's *full* advertised current.
- Impact: over-requesting on code-0 sources, and OCPTHR auto-set to 110 % of the source's maximum rather than of what this charger actually draws.
- Fix: request the current actually needed (or the documented `CURRENT_SEL=0xF`/`VOLTAGE_SEL=0xFF` maximum idiom), and expose a current argument for staged/low-current recovery requests.
- Lenses: DS, RC

**B5 · voltage-sel-zero-not-0xff** — medium — `lib/AP33772S/AP33772S.cpp:105-115`
- Wrong: fixed-PDO requests ship VOLTAGE_SEL = 0x00; the reference majority sends 0xFF ("maximum"), and one project documents that some sources validate the field despite the datasheet saying it is meaningless.
- Impact: on such a source the RDO is rejected with MSGRLT 2/3 — one of the few mechanisms that fails on exactly one PSU.
- Fix: set `rdo.REQMSG_Fields.VOLTAGE_SEL = 0xFF` (and CURRENT_SEL = 0xF for the canonical max form).
- Lenses: REF

**B6 · setuvpthr-never-writes** — medium — `lib/AP33772S/AP33772S.cpp:556-576`
- Wrong: the function encodes the value into `writeBuf[0]` and returns without ever calling `i2c_write`; every sibling setter does write. Also `readUVPTHR()` (`:537-550`) switches on the whole byte instead of masking bits 3:0.
- Impact: latent — but it is exactly the knob a developer reaches for on a board where UVP is the known problem, and it fails silently.
- Fix: add `i2c_write(AP33772S_ADDRESS, CMD_UVPTHR, 1);` and mask the read side with 0x0F.
- Lenses: DS

**B7 · reset-reads-write-only-register** — medium — `lib/AP33772S/AP33772S.cpp:159-165`
- Wrong: `reset()` does a read-modify-write on PD_CMDMSG 0x32, which the datasheet declares WO; the undefined read value is written back with reserved bits possibly set. `hardReset()` (`:173-177`) does it correctly.
- Impact: none today (no callers); a trap if ever used.
- Fix: delete `reset()` or make it a blind `writeBuf[0] = 0x01` write.
- Lenses: DS

**B8 · rdo-uninitialised-in-setfixpdo** — medium — `lib/AP33772S/AP33772S.cpp:259, 304, 352`
- Wrong: `RDO_DATA_T rdoData;` is indeterminate and `setFixPDO` never assigns VOLTAGE_SEL, which is shipped as `writeBuf[0]`; a stack byte of 0xFF means "request Maximum Voltage". The byte-order comments at `:287-288` are also inverted.
- Impact: non-deterministic PD requests from the three inherited setters (the active path `requestFixedPDO` uses `RDO_DATA_T rdo{}` and is safe).
- Fix: value-initialise all three, set VOLTAGE_SEL explicitly, and fix the comments.
- Lenses: DS

**B9 · setoutput-blind-write-no-auto-case** — low — `lib/AP33772S/AP33772S.cpp:786-802`
- Wrong: `0b00010001`/`0b00010010` select VOUTCTL correctly but replay a read-only CMDVER bit and zero two RW-reserved bits; there is no VOUTCTL=0 (Auto) case at all.
- Impact: benign on this silicon, but the firmware can never hand VOUT control back to the chip — which is why it is locked out of the VSELMIN mechanism (A23).
- Fix: read SYSTEM, mask bits 1:0, write back, and add an Auto case.
- Lenses: DS

**B10 · fork-begin-dropped-100ms-settle** — low — `lib/AP33772S/AP33772S.cpp:53-73` vs upstream `AP33772S-CentyLab/AP33772S.cpp:47-49`
- Wrong: the fork removed upstream's `delay(100)` and probes then immediately calls `refreshPdoTable()`, which can land before the first negotiation populates the table.
- Impact: `begin()` returning true is not evidence a usable PDO table exists; `_indexPPSUser/_indexAVSUser` are set from garbage.
- Fix: do not read SRCPDO in `begin()` at all — let the NEWPDO-driven refresh be the sole source.
- Lenses: DS

**B11 · i2c-read-uses-stop-not-repeated-start** — low — `lib/AP33772S/AP33772S.cpp:882-888`
- Wrong: the command phase ends with `endTransmission()` (STOP) rather than a repeated START; every other AP33772S driver uses `endTransmission(false)`. The guide's §5.2 timing figures are images and did not survive PDF→text, so this is unresolved.
- Impact: if the chip requires a repeated START, reads would be unreliable under contention on this shared bus — and would fail silently as zeros (B1).
- Fix: switch to `endTransmission(false)`, or verify against the PDF figures first.
- Lenses: DS

---

### 7.C Application layer (`main.cpp`, CurrentControl, VoltageMonitor, PowerMonitor, ThermalManager, EfficiencyEstimator)

**C1 · adc-offset-forces-max-current-into-a-dead-rail** — critical — `src/VoltageMonitor.cpp:14-15, 43`; `src/CurrentControl.cpp:84-94, 117-124`
- Wrong: `calibrated = divider*1.01854 + 1.73636` gives a hard floor of 1.736 V at raw ADC 0, so `outputVoltageV <= 0.0f` is unreachable dead code and `targetAmps = limitWatts / 1.736` saturates at `kMaxAllowedOutputAmps = 3.0 A` (step 50, reached in ~3 s at 6 steps/250 ms).
- Impact: the moment the rail is down or still ramping, the firmware commands maximum current into it — converting a one-shot collapse into a permanent, silent, self-sustaining one. Established aggravator of the reported failure.
- Fix: report 0.0 V below the ADC's valid floor, gate the divide behind a plausibility check (`< 30 V → safe()`), and reset `_lastIncreaseAt` in `safe()`.
- Lenses: APP, RC

**C2 · safe-does-not-collapse-the-42v-rail** — critical — `src/CurrentControl.cpp:145-149`; docs at `CurrentControl.h:23, 55`; callers `src/main.cpp:184, 194`
- Wrong: `safe()` writes digipot step 0, which by the module's own calibration is ~0.14 A at 42 V (~5.9 W), not off; there is no VR enable pin, and levels −1 and 0 write the identical byte. Both doc comments claim otherwise.
- Impact: every protective early-return in `loop()` ("PD not ready", "voltage too low", "disconnected", "voltage jumped") leaves 42 V live at ~0.14 A; only `requestShutdown() → pdManager.shutdown() → setOutput(0)` actually removes output.
- Fix: correct the documentation first, route genuine cut-outs through `shutdown()`, and rename `safe()` to `minimumCurrent()`. (Premise caveat: this holds only if the MCP4018 really is a current setpoint, not a voltage-feedback divider — verify against the schematic.)
- Lenses: APP, RC

**C3 · delay100-blocks-scheduler-at-turn-on** — high — `src/main.cpp:93-97`
- Wrong: `updatePd()` calls a blocking `delay(100)` on every `justAttached()` edge — which fires on re-negotiations too — in the same tick that just executed `setOutput(1)` at the new voltage.
- Impact: for 100 ms starting exactly at turn-on there is no 42 V ADC sample, no INA sample, no digipot write, no overvoltage integration — the firmware is structurally blind in the one window that matters.
- Fix: replace with a non-blocking `_settleUntilMs` gate that suppresses ramping but keeps all sampling running.
- Lenses: APP, SM

**C4 · ina238-failure-silently-zeroes-the-power-budget** — high — `src/main.cpp:113, 166-168`; `src/PowerMonitor.cpp:21-24`
- Wrong: `begin()`'s result only prints one boot line; the INA238 library returns 0.0 on any I2C failure, so `pdPowerLimit = 0` → `calculateOutputPowerLimit(0) = 0` → `CurrentControl::update(0)` → `safe()` forever. Architecturally, the PD budget should not depend on a sensor at all.
- Impact: a charger that negotiates, displays 20 V and never charges, with the only clue a boot line the user cannot see later. Fail-safe, but a diagnostic trap.
- Fix: compute the budget from `pdManager.getVoltage()`, keep a `_healthy` flag in PowerMonitor, and surface it on the display and in the periodic log.
- Lenses: APP, RC

**C5 · no-periodic-diagnostics** — high — `src/main.cpp:70-79, 185-187`; `src/CurrentControl.cpp:170`; `src/PDManager.cpp:142`
- Wrong: every diagnostic except the 5 s `[THERM]` line is printed once, on an edge; `logNoSource` is gated on `!isAttached()`, so an attached-but-stuck source produces total silence.
- Impact: directly defeats the user's stated constraint (read serial *later*, while the fault is latched) — no amount of waiting produces the needed information.
- Fix: an unconditional 2 s task, ahead of all early-returns, printing PD state, contract, VOUT/VREQ/MSGRLT, raw CONFIG/SYSTEM/MASK, latched faults, ADC raw + min/max, digipot step, INA V/A, thermal, and the shutdown reason on one repeating line.
- Lenses: APP

**C6 · min-charge-gate-uses-contract-not-measurement** — medium — `src/main.cpp:191-196`
- Wrong: the `kMinChargeVoltage = 8.5 V` gate tests `pdManager.getVoltage()` — the cached contract — although its comment describes the physical rail.
- Impact: a real sag from 20 V to 6 V never trips it, so the one guard meant to stop the charger when the VR input is too low is decoupled from the VR input (and `safe()` could not collapse the rail anyway).
- Fix: gate on `powerMonitor.getInputVoltage()` with a health check; use the contract only for the budget.
- Lenses: APP, RC

**C7 · currentlimit-reports-zero-while-0.14a-flows** — medium — `src/CurrentControl.cpp:151-156`; `src/DisplayManager.cpp:48-49`
- Wrong: `getCurrentLimitA()` returns 0.0 for `_powerLevel < 0`, but −1 and 0 write the same digipot byte (~0.14 A).
- Impact: the screen asserts "0.00 A / Max: 0.0 W" while a battery on the terminals is charged at ~0.14 A unsupervised.
- Fix: return `kCurrentAtStepZeroA` and render the disabled state as a separate tag.
- Lenses: APP

**C8 · adc-0db-range-and-42.5v-trip-margin** — medium — `src/VoltageMonitor.cpp:4-15, 27`
- Wrong: the 219 k/4.86 k divider with ADC_0db puts 42 V at ~0.91 V and the 42.5 V trip at ~0.92 V — the compressed top of ADC1's usable window — read via uncalibrated `analogRead()` with a ±0.3 V fit residual against a 0.5 V trip margin.
- Impact: a genuine 44-46 V excursion can read low and never latch; the reported "output voltage spikes" numbers are quantitatively untrustworthy.
- Fix: use `analogReadMilliVolts()`/eFuse calibration, re-scale the divider to land 42 V mid-range at ADC_11db, and set a real trip margin (~46 V) with a separate soft warning.
- Lenses: APP, BLD

**C9 · ema-filter-lags-a-partial-sag** — low — `src/VoltageMonitor.h:23-26`; `src/VoltageMonitor.cpp:47-51`
- Wrong: the EMA (α=0.1 at 100 ms ≈ 1 s τ) is symmetric, and the 5.5 V reset threshold is compared against the offset-shifted value (engaging at ~80 mV ADC input, below the usable floor).
- Impact: a 42→12 V sag is smoothed for ~1 s while CurrentControl keeps dividing by a stale high voltage; the 20-sample overvoltage integrator plus EMA lag is useless against a sub-second transient.
- Fix: use `min(raw, ema)` for the control divisor (instant on drops, smoothed on rises) and keep the slow EMA for display only.
- Lenses: APP

**C10 · efficiency-ratchets-to-0.95-and-inflates-the-budget** — medium — `src/EfficiencyEstimator.cpp:18-26`; `src/main.cpp:51-58, 122-130`
- Wrong: efficiency rises 0.005 per 50 ms tick to a 0.95 ceiling whenever there is headroom and only ever drops to a hard 0.85 floor; utilization is measured against the already-inflated `getCurrent()` denominator.
- Impact: if true VR efficiency is 0.80 the firmware permanently budgets ~19 % too much output power, compounding with B3 to roughly 2×; it can only over-drive, never starve.
- Fix: derive efficiency from measured input vs output power, or hard-code a pessimistic 0.80 and remove the estimator from the control path.
- Lenses: APP

**C11 · isrampupfinished-true-right-after-a-decrease** — low — `src/CurrentControl.cpp:110-111, 158-160`; `src/main.cpp:129`
- Wrong: a decrease is applied instantly, after which `_powerLevel >= _targetLevel`, so the flag the efficiency ratchet gates on is true one tick after any cut.
- Impact: a slow limit cycle — cut, ratchet up, re-approach, cut — which on a marginal PSU repeatedly re-triggers the source's protection.
- Fix: track the direction explicitly, or hold the estimator off for ~2 s after any decrease.
- Lenses: APP

**C12 · setup-pd-retry-loop-unbounded** — medium — `src/main.cpp:160-164`
- Wrong: `while (!pdManager.begin(...)) { showMessage(...); delay(2000); }` has no cap; `begin()` only probes 0x52, so a wedged bus or unpowered PD board traps `setup()` permanently with no voltage, thermal or current task running.
- Impact: 42 V can be live (the chip keeps VOUT force-ON across an ESP reset) with every protection suspended and the fan at 55 %; `showMessage()` clears the screen, which looks exactly like the reported flicker.
- Fix: cap the attempts, shorten the delay, fall through to a degraded loop that still runs the protective tasks, and print `esp_reset_reason()` in the boot banner.
- Lenses: APP

**C13 · periodictask-phase-locked-at-first-fire** — medium — `src/main.cpp:37-48, 134-142`
- Wrong: `_lastRun = 0` means every task is overdue on the first `loop()` iteration and fires back-to-back; the intervals (50/100/500/1000/5000) are harmonic, so they stay aligned forever.
- Impact: one iteration per second carries display + thermal + PD + ADC + INA + current + efficiency together — and it is the iteration the `delay(100)` lands in.
- Fix: add a phase parameter and de-phase the heavy tasks; replace the vestigial `75+925` with an explicit interval + phase.
- Lenses: APP

**C14 · dead-but-blocking-test-functions** — low — `src/CurrentControl.cpp:33-72`; `src/CurrentControl.h:43-49`
- Wrong: `test()`/`test_set_fixed()` have no callers anywhere but remain public API containing `delay(20000)`, `delay(10000)` and `delay(5*60*1000)`.
- Impact: none today; if called during a future debug session the charger runs at a fixed current for five minutes with every protection suspended.
- Fix: delete both, as their own TODOs say.
- Lenses: APP

**C15 · thermal-latches-pd-shutdown-before-pd-begin** — low — `src/main.cpp:154-164`; `src/ThermalManager.cpp:31-35`
- Wrong: `thermalManager.begin()` runs before `pdManager.begin()`; a missing BMP280 calls `requestShutdown → _pdManager->shutdown() → setOutput(0)` on an uninitialised PDManager, which then early-returns from `update()` forever. `ThermalManager::begin()` also takes a `TwoWire&` it never uses.
- Impact: PD negotiation permanently dead while the boot log looks normal (the screen does show "STOP: Temp failed").
- Fix: initialise PDManager first, make `shutdown()` tolerate pre-begin calls, and either use or drop the `TwoWire&` parameter.
- Lenses: APP

**C16 · thermal/overvoltage shutdown paths excluded (negative finding)** — low — `src/ThermalManager.cpp:33, 52, 70`; `src/CurrentControl.cpp:162-173`
- Established: all three ThermalManager paths and VoltageMonitor's overvoltage path always set a displayed reason string, so "no shutdown reason displayed" excludes them — *provided* the STOP line is actually on-screen (see D4, which reopens this exclusion for the flashed HEAD build).
- Fix: none for the exclusion; shorten "overtemperature" so it fits the 20-char line (D5).
- Lenses: APP

**C17 · digipot-uncontrolled-through-the-esp-boot** — high — `src/main.cpp:174-178`; `src/CurrentControl.cpp:25-31`
- Wrong: the MCP4018 is a volatile-wiper part that powers up at mid-scale (~step 64 ≈ 3.8 A, above `kMaxAllowedOutputAmps`), and `currentControl.begin()` — which writes step 0 — runs only after Serial, DiagLog, Wire, the TFT init and VoltageMonitor. On a mere 3V3 dip without POR the wiper instead *retains* the pre-brownout step.
- Impact: for a few hundred ms the VR is commanded hard exactly while VBUS steps to 20 V — an established aggravator of the reported spike-then-collapse.
- Fix: move `currentControl.begin()` to the first statement after `Wire.begin()`, verify the MCP4018 POR value, and consider a hardware hold-at-minimum while 3V3 is below POR.
- Lenses: RC

**C18 · fan-at-100-percent-duty-during-boot** — medium — `src/ThermalManager.cpp:30-38`
- Wrong: `begin()` applies full PWM duty unconditionally and only drops to the 55 % idle duty after the BMP280 probe returns.
- Impact: fan locked-rotor inrush is stacked on the shared 5 V rail during the same window as VOUT turn-on — an independent candidate for pulling VDD3P3_RTC below the brownout trip ("two boots").
- Fix: start at 0 duty, raise to idle after the sensor is up; run any full-speed self-test from `loop()` seconds later.
- Lenses: BLD

---

### 7.D Display (`DisplayManager`)

**D1 · display-samples-too-slowly-to-see-the-event** — high — `src/main.cpp:134, 138`; `src/DisplayManager.cpp:63-64`
- Wrong: `isOutputEnabled()` is evaluated only inside the 500 ms display task (its only caller anywhere), and the 42 V ADC runs at 100 ms then gets EMA-filtered; the reported event is sub-second.
- Impact: even a *correct* output-enable indicator would still read "(e)" — the transient is aliased away and nothing latches a min/max or an edge.
- Fix: latch edges and extremes in VoltageMonitor/PDManager and display the latched values, not instantaneous samples.
- Lenses: APP

**D2 · display-lines-never-padded** — medium — `src/DisplayManager.cpp:58-68`
- Wrong: all four data lines are built by concatenation and change length (17-20 chars); only the y=120 line is blanked.
- Impact: a 20 V → 0 V transition renders as e.g. `I 0V/0.0A 0.0/0.0.5` with debris from the previous frame — values are least reliable exactly during the transitions being investigated.
- Fix: `_tft.setTextPadding(240)` once in `begin()`, or pad every line to 20 chars.
- Lenses: APP

**D3 · y120-line-clipped-one-pixel** — low — `src/DisplayManager.cpp:74, 76`
- Wrong: at rotation 1 the panel is 240×135 (rows 0-134) and a size-2 glyph at y=120 occupies rows 120-135.
- Impact: the bottom row of the STOP text (and of any new PD-fault line) is clipped, with 25 px of unused slack above.
- Fix: draw at y=118, or compute it as `_tft.height() - 17`.
- Lenses: APP

**D4 · stop-line-drawn-fully-offscreen-at-y=145** — high — HEAD `src/DisplayManager.cpp` (STOP at y=145; working tree fixes it to y=118, uncommitted)
- Wrong: the *committed* version draws the shutdown reason 10 px below the bottom edge, and "Eff:" at y=120 is clipped.
- Impact: "no shutdown reason is displayed" is literally true by geometry — which means the app-level shutdown paths (C16) were never actually ruled out for the flashed build.
- Fix: commit and flash the y=118 fix and derive the y from the panel height.
- Lenses: BLD

**D5 · stop-text-truncated-at-line-width** — low — `src/DisplayManager.cpp:73-74`; `src/CurrentControl.h:62`
- Wrong: "STOP: overtemperature" is 21 chars × 12 px = 252 px on a 240 px panel (and `_shutdownReason[17]` truncates separately).
- Impact: the reason renders as "STOP: overtemperatu".
- Fix: shorten the prefix or the reason strings.
- Lenses: APP

---

### 7.E Build, config, wiring, hardware integration

**E1 · flashed-binary-not-reproducible-from-the-repo** — critical — `.pio/build/ttgo-t-display/firmware.{bin,elf}` vs `src/` vs all 20 commits
- Wrong: the "(e)/(d)" string exists in no revision of the repo and not in the last linked ELF; conversely the ELF already contains the *newer* "PD FAULT:" layout. None of DiagLog / `[PDDIAG]` / the sticky-STATUS latch / the RTC event ring is in the flashed image.
- Impact: the reported symptom describes a binary that cannot be reproduced, and the user's whole "read serial later" plan returns nothing but the old `[PD]`/`[THERM]` lines.
- Fix: rebuild and re-flash the current tree, re-observe, and embed a build ID (`__DATE__`/git hash) printed in `setup()` **and** in every periodic diagnostic line.
- Lenses: BLD, RC

**E2 · stale-inconsistent-build-directory** — critical — `.pio/build/ttgo-t-display/`; `firmware.map:1700`
- Wrong: `PDManager.cpp`, `main.cpp` and `CurrentControl.h` are all newer than their objects; `DiagLog.cpp.o` is referenced by the last link's map but absent from disk; `.sconsign311.dblite` was written by a Windows PlatformIO against `C:\Users\Markus\…` paths.
- Impact: `pio run -t upload` can trust cached signatures and flash a mixed-vintage image.
- Fix: `rm -rf .pio/build` (or `pio run -t clean`) and rebuild; never treat `firmware.bin` as evidence of what the board runs.
- Lenses: BLD

**E3 · platformio-build-currently-fails** — critical — `build_err.txt` (UTF-16), `platformio.ini` `lib_deps`
- Wrong: `pio` aborts with `SemanticVersionError: Invalid simple block '^'` while parsing the caret version specs, before compiling anything; PlatformIO is not installed in the audit environment at all.
- Impact: every planned measurement silently measures the *old* firmware, and a rebuild would be wrongly assumed to have happened.
- Fix: upgrade PlatformIO or replace the `^x.y.z` specs with exact versions, then clean-build and verify with `strings firmware.bin | grep -c PDDIAG`.
- Lenses: RC

**E4 · platform-and-libs-unpinned** — medium — `platformio.ini:12, 21-23`
- Wrong: `platform = espressif32` and caret-ranged `lib_deps`; the recorded build resolved IDF v4.4.7 / arduino-esp32 2.x.
- Impact: a fresh install resolves espressif32 6.x with different brownout/WDT/I2C defaults, and `ledcSetup()`/`ledcAttachPin()` (`ThermalManager.cpp:21,28`) were removed in arduino-esp32 3.x — the same source produces a different or non-compiling binary.
- Fix: pin `platform = espressif32@<validated version>` and exact library versions.
- Lenses: BLD

**E5 · nested-driver-copy-is-a-latent-odr-hazard** — high — `lib/AP33772S/AP33772S/AP33772S.cpp`
- Wrong: a second, pristine-upstream copy of the driver sits in a subdirectory; today PlatformIO's non-recursive Arduino-1.0 src_filter skips it (verified via the `.d` file, the archive members and the fork-only mangled symbols in the map).
- Impact: adding a `src/` folder to `lib/AP33772S` flips the filter to recursive and both `.cpp` files land in the archive — link-order-dependent mixing of an old i2c layer that hard-codes `Wire` and swallows every bus error.
- Fix: `rm -rf lib/AP33772S/AP33772S/` (upstream is already kept for diffing at `AP33772S-CentyLab/`), and pin `srcFilter` in `library.json`.
- Lenses: BLD

**E6 · stale-copies-untracked-and-not-gitignored** — medium — `.gitignore:1` (only `.pio`)
- Wrong: `lib/AP33772S/AP33772S/`, `references/`, `AP33772S-CentyLab/`, `build_err.txt` and `.vscode/` are untracked but not ignored.
- Impact: the next `git add -A` commits the duplicate driver (making E5 reproducible everywhere), ~15 third-party projects with mixed licences, and hard-coded `C:\Users\Markus` paths.
- Fix: delete the nested copy and extend `.gitignore` with those five entries.
- Lenses: BLD

**E7 · library-properties-duplicate-depends** — low — `lib/AP33772S/library.properties:10-11`
- Wrong: two `depends=` lines; the second wins, silently dropping `arduino-timer` and declaring an INA238 dependency that duplicates `lib_deps`. Neither is actually used.
- Impact: the LDF can resolve an INA238 version independently of the pinned entry — another route to a "wrong binary".
- Fix: delete the key and rename the fork so it cannot be shadowed by the registry version.
- Lenses: BLD

**E8 · gpio12-is-the-mtdi-strapping-pin** — high — `src/main.cpp:19`; `src/ThermalManager.cpp:21-30`
- Wrong: the fan PWM is on GPIO12 = MTDI, latched at reset to select VDD_SDIO (LOW → 3.3 V flash, HIGH → 1.8 V); a charged MOSFET gate decays through the ~45 kΩ internal pull-down with τ ≈ 45 µs, longer than the strapping sample window after a short brownout pulse.
- Impact: one reset can hand you a *second*, different failure — flash read fails at 1.8 V and the ROM resets again, producing exactly "two boots (screen flickers)", invisible in serial because it happens before any firmware runs.
- Fix: move the fan to GPIO 27 (or 25/26/32); additionally ensure a gate pull-*down* and/or burn `set_flash_voltage 3.3V`.
- Lenses: BLD

**E9 · brownout-detector-at-default-with-no-surviving-forensics** — high — `platformio.ini` (no brownout flag); `src/DiagLog.cpp:39, 50`
- Wrong: nothing alters the ESP32 brownout detector, so it runs at level 0 (~2.43 V); a VBUS/5 V dip at VOUT turn-on produces exactly one reset. DiagLog implements the correct RTC_NOINIT forensics — and is not in the flashed binary.
- Impact: "two boots" is fully consistent with a brownout, and there is currently no record of it whatsoever.
- Fix: re-flash so DiagLog runs; print both `esp_reset_reason()` and `rtc_get_reset_reason(0)` in the *periodic* line; add bulk capacitance (or a separate supply) on the ESP rail rather than disabling the detector.
- Lenses: BLD, RC

**E10 · serial-monitor-resets-the-board-and-wipes-the-evidence** — high — `platformio.ini:17-18` (no `monitor_dtr`/`monitor_rts`)
- Wrong: opening the port asserts DTR/RTS, which the T-Display's auto-reset pair turns into an EN pulse; an EN/power-domain reset is not guaranteed to preserve RTC slow memory, and DiagLog's magic-word check then silently falls back to a fresh log.
- Impact: the entire "attach a PC later while the fault is latched" strategy destroys the fault state on connect and looks like a clean cold boot — this alone can explain "no shutdown reason" in any post-hoc capture.
- Fix: add `monitor_dtr = 0` / `monitor_rts = 0`. (Also: `DiagLog.h:22` says 47 chars but `DiagLog.cpp:12` uses `kTextSize = 40`, clipping event strings.)
- Lenses: BLD

**E11 · i2c-has-no-clock-no-timeout-no-recovery** — high — `src/main.cpp:172` (`Wire.begin(21,22)`)
- Wrong: default 100 kHz (every reference uses 400 kHz), no `setTimeOut`, and no 9-clock bus recovery; the RotoPD Pro's pull-ups are external per the EVB guide.
- Impact: a reset mid-transaction leaves a slave holding SDA low and wedges all four devices; every call site then fails silently (`readStatus()` → 0 = "no events", `writeDigipot` leaves `_lastDigipotStep` stale), and `setup()` spins in the unbounded PD retry loop.
- Fix: run a 9-SCL recovery before `Wire.begin(sda, scl, 400000)`, add `Wire.setTimeOut(50)` (capital O), and make bus failures distinguishable from zero data.
- Lenses: BLD, RC

**E12 · evb-power-sequencing-warning-applies-in-reverse** — medium — guide line 989; `pins_and_voltages.md`
- Wrong: the guide requires the AP33772S board to be powered *before* the host because of its SDA/SCL design; here the PD trigger's logic/pull-up rail comes off the **ESP's 3V3**.
- Impact: with the ESP on PC USB and no PD source, the AP33772S is back-fed through its I2C pins and can sit in a state a later attach does not clear; on a brownout the two rails return in an uncontrolled order — a genuinely PSU-dependent variable.
- Fix: check the RotoPD Pro schematic; move the pull-ups to a VBUS-derived rail or add ~1 k series resistors in SDA/SCL; until then always attach PD before USB (which is why E10 matters doubly).
- Lenses: BLD

**E13 · pd-int-pin-is-a-floating-input** — medium — `src/PDManager.cpp:24-26` (`pinMode(_intPin, INPUT)`)
- Wrong: INT idles LOW and asserts HIGH, but GPIO13 is configured high-impedance with no pull; `update()` treats any HIGH read as an interrupt and immediately performs the *destructive* STATUS read.
- Impact: if INT is unwired or unterminated, phantom reads consume STARTED/READY/NEWPDO/UVP/OVP/OCP/OTP at unpredictable moments — a direct route to "no shutdown reason is displayed".
- Fix: `pinMode(_intPin, INPUT_PULLDOWN)` plus a startup sanity check (INT stuck HIGH while STATUS keeps reading 0 → log "INT stuck/unwired"); confirm the routing against `note_rotopd_pro_interrupt.txt`.
- Lenses: BLD

---

### Adversarial verification results (8 findings)

Note: several were partially implemented in the working tree *during* the audit; verifiers were instructed to judge the pre-fix design, so an applied fix counts as confirmation, not refutation.

| # | Finding | Verdict | Conf. | Fix sound? | Why the fix was judged unsound (one line) |
|---|---|---|---|---|---|
| V1 | **B1** i2c_read returns true on short read → `clearConfig` writes CONFIG=0x00 | **CONFIRMED** | 0.88 | **Yes** | Fail-safe in both directions and already applied; only residual gaps are the unchanged `bool` signatures and the still-unchecked `setMask/clearMask`. Trigger is narrower than "any hiccup" (address phase already fails correctly). |
| V2 | **A19** register 0x11 is VOUT but `isOutputEnabled()` cannot report MOS state | **CONFIRMED** | 0.85 | **No** | Option (1) "leave VOUTCTL at Auto" is unsafe: it reverts an empirically-rejected configuration, and its replacement signal (VREQ ≥ VSELMIN + sticky faults) is stale after a failed negotiation and blind while UVP_EN is cleared — adopt option (2) (publish the number + fault bits) only. Decisive mechanism is the 5 V force-ON, not the capacitive-hold argument. |
| V3 | **A2** protection bits masked off INT and discarded by the STATUS read | **CONFIRMED** | 0.90 | **No** | The latch + MASK=0x7F halves are correct, but "on any of bits 3-6 issue a fresh PD_REQMSG" has no rate limit, no attempt cap and no fresh-vs-sticky distinction — a self-sustaining OCP retry loop that can brown out the PD-powered ESP. Also: "(e) never (d)" is evidence *against* a latched trip on this PSU. |
| V4 | **A3** no PD_REQMSG reload after a protection trip | **CONFIRMED** | 0.85 | **No** | The `hardReset()` fallback drops VBUS and reboots the PD-powered ESP (manufacturing the very "two boots" symptom); the staged 5 V step is not sequenced with CurrentControl (4× input current); "modest CURRENT_SEL" is inexpressible in the fork (it hard-codes PDO max); PDO1 is assumed to be 5 V; `MSGRLT==1` is sticky; a flat 1 s retry is tighter than every reference; and `shutdown()` as the terminal state may kill the MCU's own supply. |
| V5 | **A2/A4** STATUS fault bits destroyed; only one reader, at 1 Hz | **CONFIRMED** | 0.92 | **No** | "Clear the latch on a successful re-negotiation (ESPHome semantics)" is self-defeating here — recovery *is* a re-negotiation, so a fault erases its own evidence; and the paired poll-rate increase is only safe once PD_REQMSG rate limiting exists. Minor: `PD_MSGRLT` must be masked with 0x07. |
| V6 | **A5** 1 Hz tick makes every sub-second PDManager constant dead | **CONFIRMED** | 0.95 | **Yes** | Sound and already applied (50 ms), but the stated *rationale* is inverted — read-clear bits latch until read, so faster polling shreds evidence sooner; it is safe only alongside the sticky latch and the detach debounce, and it exposes an unrate-limited `refreshPdoTable()` when `requestBestPdo()` fails without re-stamping `_stateEnteredAt`. |
| V7 | **A6** VOUT forced ON ~1 s before the 20 V request (wall-clock timeline) | **CONFIRMED** | 0.86 | **No** | Half 2 (50 ms tick) is right and applied; half 1 (force VOUT on at 5 V then wait for a stable 5 V before requesting) targets a non-problem — VOUT is already live at 5 V from attach via VOUTCTL=auto + VSELMIN 5000 mV — does not reduce the 5→20 V inrush energy, has no timeout (stalls forever on a non-PD source), and would re-introduce a blocking wait. |
| V8 | **A7** VOUT force-ON before the request is the inrush cause; "every reference does the opposite" | **REFUTED** | 0.72 | **No** | Survey claim is false (CentyLab's own RotoPD Pro examples enable before requesting), and with VSELMIN at 5000 mV AUTO closes the MOS at 5 V anyway, so force-ON changes nothing electrically. The proposed fix would *deadlock* the current machine (Ready requires `voutArrived` measured on the gated-off VOUT node), breaks the presence/liveness checks, is a straight revert to the commit the user titled "pd does not work", and hard-closes the MOS onto discharged caps at 20 V — plausibly the harsher transient. |

**Net verification take-away:** 7 of 8 confirmed as *defects*, but in three of them the verifier explicitly severed the defect from the causal attribution — the fault-bit/recovery findings explain why the failure is **undiagnosable**, not why it happens. The "(e) never (d)" evidence points away from a latched chip protection and toward a contract collapse to vSafe5V with the VOUT MOS still closed (A1), which is the established root cause.

---

## 8. Change inventory

| File | What changed | Why | Whose change |
|---|---|---|---|
| `/tmp/test/charger/src/DiagLog.h` *(new, untracked)* | New `DiagLog` namespace: `begin()`, `event(fmt,…)`, `dump()`, `bootCount()`, `resetReasonText()`, `setStickyFlags()/stickyFlags()`, `kCapacity = 40`. | The fault is only observable minutes later, over a serial port attached after the fact, so nothing may be printed once and lost. | **Assistant** (whole file) |
| `/tmp/test/charger/src/DiagLog.cpp` *(new, untracked)* | 40-entry timestamped ring buffer + boot counter + 1 sticky flag byte, all in `RTC_NOINIT_ATTR` memory guarded by magic `0x50444347`; replays the previous boot's events and the `esp_reset_reason()` on `begin()`; `event()` self-initialises if called before `begin()`. | RTC_NOINIT survives brownout/panic/WDT resets (not a cold power cycle), so evidence recorded right before the reset the fault caused is still there afterwards. | **Assistant** (whole file) |
| `/tmp/test/charger/src/PDManager.cpp` | **Assistant:** `pollStatus()` as the single choke point for the read‑clear STATUS register, OR‑accumulating every bit into `_statusSticky` and mirroring it to RTC memory; `kFaultMask = UVP|OVP|OCP|OTP`; `getFaultText()` / `hasChipFault()`; `logDiagnostics()` one-line `[PDDIAG]` register dump; VOUT 3-band transition logging (`OFF(<1V)` / `LOW(~5V)` / `HIGH`); **contract-liveness check in `Ready`** (VOUT < 0.75 × contract for ≥300 ms ⇒ dead contract); `attemptRecovery()` with 2 s backoff and a 5-attempt ceiling, wired to protection trips, chip restart (`STARTED` seen outside `Disconnected`) and contract loss; strict `Requesting` acceptance (`msgrlt==1` **and** VREQ matches **and** VOUT actually arrived) replacing the old `READY \|\| msgSuccess \|\| voltageMatched`; explicit `msgrlt>=2` rejection path with named reasons and request backoff; `INPUT_PULLDOWN` on the INT pin; periodic 100 ms STATUS fallback poll; time-based detach debounce (`_absentSinceMs` / 500 ms) replacing the user's call counter; CONFIG/MASK read-back drift repair in `Ready`; re-negotiate on best **voltage** difference instead of PDO **index**; `kMinUsablePdoCurrentA` filter in `findBestFixedPdo()`; `getVoutVoltageV()`; `DiagLog::event()` calls on attach/detach/request/ready/timeout/shutdown; sticky-flag carry-over in `begin()`. **Pre-existing:** `applyStartupConfig()` deferring CONFIG/MASK/SYSTEM until `STARTED=1` with force-**ON** VOUT at 5 V (incl. the `// TODO: i dont want to disable them!` notes), `isOutputEnabled()`, removal of the `setOutput(0)` during re-negotiation. | The firmware only ever compared the source's *advertised* PDO list to the cached contract, so a 20 V contract collapsing to vSafe5V with the MOS still closed was structurally invisible; and every STATUS read destroyed the evidence of the chip-side fault that caused it. | **Mixed** (majority assistant; `applyStartupConfig`/force-ON, `isOutputEnabled`, no-setOutput(0)-on-recheck are pre-existing) |
| `/tmp/test/charger/src/PDManager.h` | **Assistant:** declarations for `getVoutVoltageV()`, `logDiagnostics()`, `getStickyStatus()`, `getFaultText()`, `hasChipFault()`, `getState()`, private `pollStatus()`, `attemptRecovery()`, `kMinUsablePdoCurrentA = 1.25f`; new members `_lastStatusPollMs`, `_absentSinceMs`, `_contractLostSinceMs`, `_recoveryAttempts`, `_lastRecoveryMs`, `_recoveryExhausted`, `_nextRequestAllowedMs`, `_statusLast`, `_statusSticky`, `_lastVoutBand`. **Pre-existing:** `isOutputEnabled()`, private `applyStartupConfig()`, `_configured`. The user's `_absentCount` member is gone (superseded, so it never appears in the diff). | Exposes the sticky fault + raw VOUT to the display and the log; holds the liveness/recovery/backoff state. | **Mixed** |
| `/tmp/test/charger/src/DisplayManager.cpp` | **Assistant:** removed the `(e)`/`(d)` boolean flag and replaced it with a numeric `in <VOUT>V` readout appended to the `Max:` line; merged `Temp:` and `Eff:` onto one line at y=95; added the `PD FAULT: <text>` bottom line fed by `hasChipFault()`/`getFaultText()`; STOP line nudged 120 → 118; PD current now printed with 1 decimal (`1.5A` must not round to `2A`); `"PD "` label shortened to `"I "`. **Pre-existing:** the `(e)`/`(d)` flag itself (its padding space on the `Out:` line survives), the STOP line move from y=145 to y=120. | `(e)` came from `isOutputEnabled()` testing `readVoltage() >= 1000 mV`; register 0x11 is VOUT, so a collapsed 5 V contract still read "enabled". A number next to the contract voltage makes the collapse visible; the fault line is why no shutdown reason was ever shown. | **Mixed** |
| `/tmp/test/charger/src/DisplayManager.h` | `update()` first parameter de-const'd: `const PDManager &` → `PDManager &`. | Needed by the user's non-const `isOutputEnabled()`; retained by the assistant because `getVoutVoltageV()` is likewise non-const (it does an I2C read). | **Pre-existing** (kept by assistant) |
| `/tmp/test/charger/src/main.cpp` | `DiagLog::begin()` as the first call in `setup()`; `currentControl.begin()` moved to be the **first** I2C transaction, before the display/thermal init; PD task period `75+925` → **50 ms** (with the reasoning comment); new `pdDiagTask` (2 s, `logPdDiagnostics`) and `diagHistoryTask` (30 s, `DiagLog::dump()`), both ticked *ahead of every early return* in `loop()`; removed the blocking `delay(100)` after attach. | The MCP4018 powers up at mid-scale on the same 3V3 rail while the VBUS-powered AP33772S still holds VOUT on, so the regulator is driven hard through the whole boot — the digipot must be zeroed first. The 1 s tick made every sub-second constant in PDManager fire on every call. | **Assistant** (file was clean at session start) |
| `/tmp/test/charger/src/VoltageMonitor.cpp` / `.h` | Below `kCalibrationValidFromV = 2.0f` raw divider volts, the calibrated output is forced to `0.0f` instead of extrapolating the 30–41 V straight-line fit. | The +1.736 V intercept made a dead rail read 1.74 V, which passed the `outputVoltageV <= 0` guard and then became the divisor in `targetAmps = limitWatts / outputVoltageV`, pinning demand at the 3 A ceiling exactly when the rail was down. | **Assistant** |
| `/tmp/test/charger/src/CurrentControl.cpp` | Reads the MCP4018 wiper *before* overwriting it and logs it via `DiagLog`; `safe()` now re-stamps `_lastIncreaseAt`. | Turns the "powers up at mid-scale" datasheet claim into a measurement on this board (a value near 64 after a reboot confirms it); without the re-stamp the first `update()` after the gate reopens jumps a full accelerated step instead of ramping. | **Assistant** |
| `/tmp/test/charger/src/CurrentControl.h` | Added `getDigipotStep()`, `getPowerLevel()`, `getTargetLevel()`. | `logDiagnostics()` needs the commanded digipot step in the `[PDDIAG]` line. | **Assistant** |
| `/tmp/test/charger/src/ThermalManager.cpp` | Fan starts at `kFanMinimumDuty` instead of `kFanFullDuty`. | Full duty put the fan's inrush on the shared 5 V rail during exactly the window in which the regulator's input caps charge and the ESP is most brownout-prone. | **Assistant** |
| `/tmp/test/charger/lib/AP33772S/AP33772S.cpp` | `i2c_read()` returns **false** on a short read instead of silently succeeding with a zeroed `readBuf`; `setMask`/`clearMask`/`setConfig`/`clearConfig` bail on a failed read; `isFixedPdoAt()` uses the DETECT bit (bit 15) instead of the "both bytes zero" heuristic; `getPdoMaxCurrentA()` returns **0.0 A** (not 1.0 A) for CURRENT_MAX code 0. | A failed CONFIG read made `clearConfig()` write `0x00`, disabling **every** protection (DR/OTP/OCP/OVP/UVP). Code 0 is the "<1.24 A" bucket, so returning 1 A over-budgeted power by up to 2×. | **Assistant** |
| `/tmp/test/charger/platformio.ini` | Added `monitor_dtr = 0` and `monitor_rts = 0`. | On the T-Display those lines drive the auto-reset pair; opening `pio device monitor` pulsed CHIP_PU and wiped every in-RAM latch — the whole "attach the PC later, while the fault is still latched" workflow depends on this. | **Assistant** |
| `/tmp/test/charger/KOMPONENTEN.md` | Deleted (unstaged deletion). | Not part of this work. | **Pre-existing** |
| Untracked, pre-existing: `.vscode/`, `AP33772S-CentyLab/`, `AP33772S-Sink-Controller-EVB-User-Guide.pdf/.txt`, `build_err.txt`, `lib/AP33772S/AP33772S/`, `note_rotopd_pro_interrupt.txt`, `references/` | Unchanged by this work. | — | **Pre-existing** |
| `/tmp/test/charger/PD-DEBUG-SUMMARY.md` | Untracked; **appeared at 11:13:21 during this inventory run** — it was absent from the first `git status --short` a few seconds earlier. Not part of the firmware change set. | Written concurrently by something else in this session. | Flagged, unattributed |

### New public API

**New files**
- `src/DiagLog.h`, `src/DiagLog.cpp`

**`namespace DiagLog` (all new)**
- `constexpr uint8_t kCapacity = 40`
- `void begin()`
- `void event(const char *fmt, ...)`
- `void dump()`
- `uint32_t bootCount()`
- `const char *resetReasonText()`
- `void setStickyFlags(uint8_t)` / `uint8_t stickyFlags()`

**`PDManager` — new public members (assistant)**
- `float getVoutVoltageV()`
- `void logDiagnostics(float inaVoltageV, float inaCurrentA, float outputVoltageV, int digipotStep)`
- `uint8_t getStickyStatus() const`
- `const char *getFaultText() const`
- `bool hasChipFault() const`
- `PDNegotiationState getState() const`

**`PDManager` — new public member (pre-existing)**
- `bool isOutputEnabled()`

**`CurrentControl` — new public members (assistant)**
- `int16_t getDigipotStep() const`
- `int16_t getPowerLevel() const`
- `int16_t getTargetLevel() const`

**New non-public constants / helpers (assistant, listed for completeness)**
- `PDManager::kMinUsablePdoCurrentA = 1.25f` (private); private `applyStartupConfig()` (pre-existing), `pollStatus()`, `attemptRecovery(const char *reason)`
- File-scope in `PDManager.cpp`: `kFaultMask`, `kStatusPollMs = 100`, `kAbsentConfirmMs = 500`, `kContractLiveFraction = 0.75f`, `kContractLostMs = 300`, `kRecoveryBackoffMs = 2000`, `kMaxRecoveryAttempts = 5`
- `VoltageMonitor::kCalibrationValidFromV = 2.0f` (private)
- `platformio.ini`: `monitor_dtr`, `monitor_rts`

**Changed (not new) public signatures / semantics**
- `DisplayManager::update()` — first parameter is now `PDManager &` (non-const); **source-breaking** for any caller passing a const reference.
- `AP33772S::getPdoMaxCurrentA()` now returns `0.0f` for CURRENT_MAX code 0 (was `1.0f`); `AP33772S::isFixedPdoAt()` now keys on DETECT; `i2c_read()` now reports short reads as failures.

### Compile status

**Exactly one successful `pio run` exists**, finishing **2026‑08‑31 23:41:47** (`/tmp/test/charger/.pio/build/ttgo-t-display/firmware.bin`; objects written 23:41:44–23:41:45). It compiled `DiagLog.cpp` (object present: `.pio/build/ttgo-t-display/src/DiagLog.cpp.o`) plus the state of the sticky-STATUS / `logDiagnostics` / display work **as it stood at 23:41**.

**Every single changed file has been edited since that build.** Nothing currently in the working tree has been compiled in its present form — including `DiagLog.h`/`DiagLog.cpp` themselves, which were modified again the next morning. There is no build covering the current tree, and no evidence of any build attempt after 23:41:47.

Files edited after the successful build (mtime, all `> 2026‑08‑31 23:41:47`):

| File | mtime |
|---|---|
| `/tmp/test/charger/src/DisplayManager.cpp` | 2026‑08‑31 23:55:24 |
| `/tmp/test/charger/src/DisplayManager.h` | 2026‑08‑31 23:55:24 |
| `/tmp/test/charger/src/VoltageMonitor.cpp` | 2026‑08‑31 23:55:24 |
| `/tmp/test/charger/src/VoltageMonitor.h` | 2026‑08‑31 23:55:24 |
| `/tmp/test/charger/src/CurrentControl.h` | 2026‑08‑31 23:55:24 |
| `/tmp/test/charger/src/CurrentControl.cpp` | 2026‑08‑31 23:59:03 |
| `/tmp/test/charger/src/main.cpp` | 2026‑09‑01 10:41:00 |
| `/tmp/test/charger/platformio.ini` | 2026‑09‑01 10:50:07 |
| `/tmp/test/charger/src/ThermalManager.cpp` | 2026‑09‑01 10:50:20 |
| `/tmp/test/charger/lib/AP33772S/AP33772S.cpp` | 2026‑09‑01 11:04:19 |
| `/tmp/test/charger/src/PDManager.h` | 2026‑09‑01 11:04:33 |
| `/tmp/test/charger/src/DiagLog.h` | 2026‑09‑01 11:04:45 |
| `/tmp/test/charger/src/DiagLog.cpp` | 2026‑09‑01 11:04:58 |
| `/tmp/test/charger/src/PDManager.cpp` | 2026‑09‑01 11:05:12 |

Highest-risk uncompiled items, since they are the largest and most recent: the `PDManager` rewrite (`pollStatus`, `attemptRecovery`, contract liveness, `logDiagnostics`, `getFaultText`), the `DisplayManager` signature change, the `AP33772S` library edits, and the `main.cpp` task-table changes. `build_err.txt` in the repo root is a stale PowerShell/`pio` failure from 2026‑08‑03 and is unrelated.


---

## 9. Open items and decisions

Everything below is **not applied**. Items 9.2 and 9.3 need decisions or hardware knowledge
that only the project owner has.

### 9.1 — Small, unambiguous, not yet done

| Item | Where | Note |
|---|---|---|
| `_voltage`/`_current` published from the *request*, not from `readVREQ()`/`readIREQ()` on success | `PDManager::requestBestPdo()` | The liveness check covers the consequence, not the cause. Keep `_requestedVoltage` separate and publish only at the Ready transition. Also zero them on timeout and in `shutdown()` |
| `shutdown()` leaves `_state == Ready` | `PDManager::shutdown()` | `isReady()`/`getVoltage()` keep advertising a live contract after a latched shutdown |
| No `Wire.setTimeOut(50)`, no 9-pulse bus recovery | `main.cpp` `Wire.begin()` | Capital **O** — `Stream::setTimeout()` is a different, silently useless function |
| INT is polled, never `attachInterrupt`-driven | `PDManager::update()` | `note_rotopd_pro_interrupt.txt` describes the intended path. Cheap middle ground: sample GPIO13 every `loop()` iteration outside the 50 ms task |
| Nested `lib/AP33772S/AP33772S/` untracked *and* not gitignored | `.gitignore` (only entry: `.pio`) | `git add -A` would commit a stale second driver |
| `CurrentControl::test()` / `test_set_fixed()` dead code with `delay(5*60*1000)` | `CurrentControl.cpp` | Unreachable today; would freeze every protection task if ever called |
| `getCurrentLimitA()` returns 0.0 for level −1 | `CurrentControl.cpp` | Hardware still permits ~0.14 A; the class docs claim `safe()` is an off switch, which is false |
| `platform = espressif32` unpinned | `platformio.ini` | Same source can produce a materially different binary elsewhere |

### 9.2 — reduce inrush
Three candidate levers. **Only one actually reduces inrush; do all three, but for different reasons.**

**(a) Staged voltage request — this is the inrush fix.** Inrush into the VR's input capacitance is `i = C · dV/dt` and delivered charge per step is `Q = C · ΔV`. Splitting 5→20 V into 5→9→15→20 cuts the peak of each transition by roughly 3×, and the source re-slews per transition. Dwelling at 5 V achieves nothing (a dwell does not change the size of the *next* step) — the step count is what matters.
**File:** `src/PDManager.cpp` — **function:** `requestBestPdo()`. Non-blocking, driven by the existing 50 ms tick:
```cpp
// PDManager.h
uint8_t _finalPdoIndex = 0;   // the PDO we are working toward
uint8_t _stagePdoIndex = 0;   // the PDO currently requested

// PDManager.cpp — pick the next fixed PDO at most kStageStepV above the present VOUT.
static constexpr float kStageStepV = 6.0f;

uint8_t PDManager::nextStagePdo(float presentV, uint8_t finalPdo) const {
    const float finalV = _pd.getPdoVoltageV(finalPdo);
    if (finalV <= presentV + kStageStepV) return finalPdo;   // last hop
    uint8_t best = 0; float bestV = -1.0f;
    for (int p = 1; p <= MAX_PDO_ENTRIES; ++p) {
        const float v = _pd.getPdoVoltageV(p);
        if (v <= presentV + 0.5f || v > presentV + kStageStepV) continue;
        if (v > bestV) { bestV = v; best = static_cast<uint8_t>(p); }
    }
    return best ? best : finalPdo;   // no intermediate slot exists: go direct
}
```
On each successful `Requesting`→`Ready`, if `_stagePdoIndex != _finalPdoIndex`, force `currentControl.safe()` (already wired via `_voltageJumped` → `src/main.cpp:90-92`), wait one `kAttachSettleMs`, and request the next stage. **Cap the total staging time** (e.g. 3 s) and go direct if it expires, so a source with a sparse PDO list cannot stall the machine.

**(b) Lower `CURRENT_SEL` — a correctness fix, not an inrush fix.** It does not change the capacitive step, but it stops the RDO claiming 5 A from a source that will then size its own OCP and budget accordingly, and it stops `getPdoMaxCurrentA()`'s number from driving the digipot target.
**File:** `lib/AP33772S/AP33772S.cpp:105-115`:
```cpp
bool AP33772S::requestFixedPDO(uint8_t pdoIndex, int maxCurrentMa)
{
    if (!isFixedPdoAt(pdoIndex)) return false;
    const int code = currentMap(maxCurrentMa);
    if (code < 0 || code > SRC_SPRandEPRpdoArray[pdoIndex - 1].fixed.current_max) return false;
    RDO_DATA_T rdo{};
    rdo.REQMSG_Fields.PDO_INDEX   = pdoIndex;
    rdo.REQMSG_Fields.VOLTAGE_SEL = 0;          // ignored for fixed PDOs, but be explicit
    rdo.REQMSG_Fields.CURRENT_SEL = static_cast<unsigned>(code);
    writeBuf[0] = rdo.byte0;
    writeBuf[1] = rdo.byte1;
    return i2c_write(AP33772S_ADDRESS, CMD_PD_REQMSG, 2);
}
```
Call it with the board's real ceiling (3 A × 42 V / 20 V ÷ efficiency ≈ 8 A input is impossible, so the real constraint is `kMaxAllowedOutputAmps` reflected to the input): request ~3000 mA, not the PDO maximum.

**(c) Set `OCPTHR` explicitly — an observability/safety fix.** Leaving it at 0 means the trip point is 110 % of the *source's* IMAX (§7.2, :3446-3447) — so the chip never trips and a source-side collapse arrives with no STATUS bit at all. Setting it converts a silent collapse into a reportable `STATUS.OCP`.
**File:** `src/PDManager.cpp` — **function:** `applyStartupConfig()` (`:62-83`), after the CONFIG write:
```cpp
    _pd.setOCPTHR(3200);   // 50 mA/LSB -> 0x40; trips at 110% = 3.52 A
```

**(d) `VSELMIN` — only together with (a).** `_pd.setVSELMIN(9000)` (`AP33772S.cpp:527`) makes the chip keep the MOS open until VREQ ≥ 9 V, the datasheet's own recipe for this board (§8.4, :3648). **But on its own it makes things worse**: it converts a rate-limited 5→20 V slew through a closed MOS into a hot-plug of 20 V onto discharged caps — the failure mode the code comment at `src/PDManager.cpp:76-80` records as having previously crashed the board. Adopt it only once staged stepping is in, and only if measurement shows the VR is unhappy at 5 V.

**Hardware, and the real answer:** the README TODO already identifies it — the digipot sits in the VR's GND path, so `currentControl.safe()` is a ~0.14 A *minimum-current setpoint* (`src/CurrentControl.cpp:22`; `getCurrentLimitA()` at `:172-177` misleadingly returns 0.0 for level −1), **not an off switch**. Until the VR gets a real high-side enable line, no firmware change can actually disconnect the load during a transition. Fix the `safe()` documentation in `src/CurrentControl.h:23` first so no future reasoning depends on the false claim.

### 9.3 — NTC / TR25..TR100 vs OTP
**This depends on hardware the user must confirm: is an NTC thermistor actually fitted to the AP33772S's OTP pin on the RotoPD Pro?** Not determinable from the repo — no schematic, and `pins_and_voltages.md` does not mention it. **Do not guess.**
§7.4 (:3505-3507): *"The host MCU must initialize the TR25, TR50, TR75, and TR100 registers before reading the TEMP register or enabling OTP and power de-rating functions."* Today CONFIG stays at F8h except for UVP (`src/PDManager.cpp:70`), so `OTP_EN` and `DR_EN` are armed against an uninitialised table.
**File:** `src/PDManager.cpp` — **function:** `applyStartupConfig()`:
```cpp
// CASE A — NTC IS fitted (confirm from the RotoPD Pro schematic first).
// Values are the datasheet's 10 K B-curve defaults; substitute the fitted part's.
_pd.setNTC(10000, 4161, 1928, 974);   // blocking, ~15 ms (AP33772S.cpp:444-461)
_pd.setOTPTHR(100);                   // 1 C/LSB; below the 120 C default
// Then trust readTemp() and act on STATUS.OTP.

// CASE B — NO NTC fitted. Stop the chip acting on a fabricated temperature.
_pd.clearConfig(AP33772_CONFIG::OTP_EN);
_pd.clearConfig(AP33772_CONFIG::DR_EN);
// The board already has its own BMP280-based ThermalManager for real thermal limiting.
```
**The `[PDDIAG]` line decides it for you without a schematic:** `Tchip=` already prints `readTemp()` (`src/PDManager.cpp:155`, `:180`). A plausible 20-45 °C that tracks the fan means an NTC is present and roughly calibrated → Case A. A value pinned at 0, at 255, or wildly implausible means no NTC or an open pin → Case B. **Run the diagnostic first, then pick the branch.**
