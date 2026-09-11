#include "PDManager.h"

#include "DiagLog.h"

#include <math.h>
#include <string.h>

// Every AP33772S protection bit. STATUS carries these whether or not they are
// unmasked; the mask only gates the physical INT pin.
static constexpr uint8_t kFaultMask = UVP_MSK | OVP_MSK | OCP_MSK | OTP_MSK;

// How long to wait for a requested contract to become active before retrying.
static constexpr uint32_t kNegotiationTimeoutMs = 2000;
// While Ready, how often to re-check the source's profile list for changes
// (in addition to reacting immediately to the NEWPDO status bit).
static constexpr uint32_t kProfileRecheckMs     = 500;
// How often to poll STATUS as a fallback when the INT pin is not asserting.
static constexpr uint32_t kStatusPollMs         = 100;
// How long the source must look absent before we believe it. Time-based, not a
// call counter: the update rate is a scheduling detail, the debounce is not.
static constexpr uint32_t kAbsentConfirmMs      = 500;
// A contract is considered dead when VOUT sits below this fraction of the
// contract voltage. 0.75 clears a 20 V contract collapsing to vSafe5V (5/20 =
// 0.25) by a wide margin while tolerating cable and MOS drop under load.
static constexpr float    kContractLiveFraction = 0.75f;
// How long VOUT must stay low before the contract is declared dead. Longer than
// the chip's 30 ms protection debounce and than a legitimate PDO transition.
static constexpr uint32_t kContractLostMs       = 300;
// Wait between recovery attempts, so a source that refuses to co-operate cannot
// be hammered with back-to-back PD_REQMSGs.
static constexpr uint32_t kRecoveryBackoffMs    = 2000;
// Give up (and say so) after this many consecutive failed recoveries.
static constexpr uint8_t  kMaxRecoveryAttempts  = 5;

bool PDManager::begin(TwoWire &wire, int8_t intPin) {
    _intPin = intPin;
    if (_intPin >= 0) {
        // PULLDOWN, not a bare INPUT. The AP33772S drives INT actively HIGH
        // when asserted, so a defined low is the correct idle state. Left
        // floating, a bare input reads HIGH at random - and update() treats a
        // HIGH as "interrupt asserted" and immediately calls pollStatus(),
        // which is the read that CLEARS STATUS in hardware. A floating pin
        // therefore silently eats STARTED/READY/NEWPDO and the protection bits
        // at unpredictable moments, which at the new 50 ms tick would happen up
        // to 20 times a second.
        pinMode(_intPin, INPUT_PULLDOWN);
    }

    // Carry any fault recorded before a brownout into this boot, so the display
    // and the [PDDIAG] line still report it after the reset it caused.
    _statusSticky = DiagLog::stickyFlags();
    if (_statusSticky != 0) {
        DiagLog::event("sticky STATUS carried over: 0x%02X", _statusSticky);
    }

    // Only probe the I2C bus here. CONFIG/MASK/SYSTEM writes are deferred until
    // the controller reports STARTED=1 (see applyStartupConfig), because the
    // datasheet requires configurable registers to be written after the sink
    // controller has started and within the 100 ms window before the 1st
    // negotiation.
    return _pd.begin(wire);
}

// Applies the startup configuration once, after the controller reports
// STARTED=1. Per the datasheet these writes must happen after start and before
// the first negotiation completes.
void PDManager::applyStartupConfig() {
    if (_configured) {
        return;
    }
    // UVP must be disabled for the RotoPD Pro (avoids UVP hard resets).
    // The other AP33772S protections stay at their power-on defaults.
    _pd.clearConfig(AP33772_CONFIG::UVP_EN);
    // Unmask the protection bits too: MASK only gates the INT pin, and leaving
    // UVP/OVP/OCP/OTP masked meant a protection trip never raised INT and was
    // only ever seen (and destroyed) by the periodic STATUS poll.
    _pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask);
    _configured = true;
}

void PDManager::setMeasuredRailVoltage(float volts) {
    _railVoltageV = volts;
    _railValid    = isfinite(volts) && (volts >= kRailValidMinV);

    if (_railValid != _railWasValid) {
        DiagLog::event("INA VBUS measurement %s (%.2fV)",
                       _railValid ? "available" : "LOST", volts);
        _railWasValid = _railValid;
    }
}

// The single choke point for STATUS. The register is read-clear: whatever this
// read returns is gone from the chip forever, so every bit is OR-accumulated
// into _statusSticky before the caller gets a chance to ignore it.
uint8_t PDManager::pollStatus() {
    const uint8_t status = _pd.readStatus();
    if (status == 0) {
        return 0;
    }

    _statusLast = status;
    // Seed from RTC-retained memory so a fault recorded before a brownout is
    // not erased by the reset that fault caused.
    _statusSticky |= DiagLog::stickyFlags();
    const uint8_t fresh = static_cast<uint8_t>(status & ~_statusSticky);
    _statusSticky |= status;
    DiagLog::setStickyFlags(_statusSticky);

    const uint8_t faults = static_cast<uint8_t>(status & kFaultMask);
    if (faults != 0) {
        DiagLog::event("CHIP FAULT status=0x%02X%s%s%s%s", status,
                       (faults & UVP_MSK) ? " UVP" : "",
                       (faults & OVP_MSK) ? " OVP" : "",
                       (faults & OCP_MSK) ? " OCP" : "",
                       (faults & OTP_MSK) ? " OTP" : "");
    } else if (fresh != 0) {
        DiagLog::event("status=0x%02X%s%s%s", status,
                       (fresh & STARTED_MSK) ? " STARTED" : "",
                       (fresh & READY_MSK)   ? " READY"   : "",
                       (fresh & NEWPDO_MSK)  ? " NEWPDO"  : "");
    }

    return status;
}

const char *PDManager::getFaultText() const {
    // Static buffer: this is only ever called from the display and the logger,
    // both on the single Arduino loop task.
    static char text[20];
    text[0] = '\0';

    const uint8_t faults = static_cast<uint8_t>(_statusSticky & kFaultMask);
    if (faults == 0) {
        return text;
    }

    if (faults & UVP_MSK) { strcat(text, "UVP "); }
    if (faults & OVP_MSK) { strcat(text, "OVP "); }
    if (faults & OCP_MSK) { strcat(text, "OCP "); }
    if (faults & OTP_MSK) { strcat(text, "OTP "); }
    return text;
}

bool PDManager::hasChipFault() const {
    return (_statusSticky & kFaultMask) != 0;
}

// One self-contained line carrying everything needed to tell the failure modes
// apart after the fact. Deliberately does NOT read STATUS (read-clear, owned by
// update()); it prints the sticky latch instead.
void PDManager::logDiagnostics(float inaVoltageV, float inaCurrentA,
                               float outputVoltageV, int digipotStep,
                               const char *inaAlertText) {
    static const char *kStateNames[] = {"DISC", "SETTLE", "REQ", "READY"};

    uint8_t config = 0, mask = 0, system = 0;
    _pd.readRawByte(CMD_CONFIG, config);
    _pd.readRawByte(CMD_MASK, mask);
    _pd.readRawByte(CMD_SYSTEM, system);

    const uint8_t opMode  = _pd.readOpMode();
    const uint8_t msgRlt  = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
    const int     voutMv  = _pd.readVoltage();   // register 0x11 is VOUT, not VBUS
    const int     vreqMv  = _pd.readVREQ();
    const int     ireqMa  = _pd.readIREQ();
    const int     ioutMa  = _pd.readCurrent();
    const int     chipTmp = _pd.readTemp();

    // VOUTCTL is SYSTEM bits 1:0 - 0 auto, 1 force OFF, 2 force ON.
    const uint8_t voutCtl = static_cast<uint8_t>(system & 0x03);

    // Classify the MEASURED rail into three bands and log the transitions.
    // This used to classify register 0x11, which on this board is pinned near
    // 5 V regardless of the contract, so the band never changed and the log
    // said "LOW(~5V)" forever. The INA238 is the only sensor that actually sees
    // this rail.
    const float   railV = _railVoltageV;
    const uint8_t band  = !_railValid ? 0 : (railV < 7.0f ? 1 : 2);
    if (band != _lastRailBand) {
        static const char *kBandNames[] = {"NO-MEAS", "LOW(~5V)", "HIGH"};
        DiagLog::event("rail %s -> %s (%.2fV) contract=%.1fV",
                       (_lastRailBand <= 2) ? kBandNames[_lastRailBand] : "?",
                       kBandNames[band], railV, _voltage);
        _lastRailBand = band;
    }

    const bool hasInaAlert = (inaAlertText != nullptr && inaAlertText[0] != '\0');

    // r11 is register 0x11, printed as evidence only - it is NOT the rail. rail
    // is the INA238 measurement that every decision now uses; NO-MEAS means the
    // sensor is absent and the rail-dependent checks are being skipped.
    Serial.printf(
        "[PDDIAG] st=%s contract=%.1fV/%.2fA pdo=%u | rail=%.2fV%s r11=%.2fV VREQ=%.2fV "
        "IREQ=%.2fA Iout=%.2fA Tchip=%dC | status=0x%02X sticky=0x%02X%s%s%s%s "
        "msgrlt=%u opmode=0x%02X cfg=0x%02X mask=0x%02X sys=0x%02X voutctl=%u | "
        "ina=%.2fV/%.2fA out=%.2fV pot=%d | boot#%u %s\n",
        kStateNames[static_cast<uint8_t>(_state)],
        _voltage, _current, _activePdoIndex,
        _railVoltageV, _railValid ? "" : "(NO-MEAS)",
        voutMv / 1000.0f, vreqMv / 1000.0f, ireqMa / 1000.0f, ioutMa / 1000.0f, chipTmp,
        _statusLast, _statusSticky,
        hasChipFault() ? " FAULT:" : "", getFaultText(),
        hasInaAlert ? " INA_FAULT:" : "", hasInaAlert ? inaAlertText : "",
        msgRlt, opMode, config, mask, system, voutCtl,
        inaVoltageV, inaCurrentA, outputVoltageV, digipotStep,
        DiagLog::bootCount(), DiagLog::resetReasonText());
}

bool PDManager::isSourcePhysicallyPresent() {
    const uint8_t opMode = _pd.readOpMode();
    // Bit 1 = PDMOD (PD source), Bit 0 = LGCYMOD (Legacy non-PD source)
    if ((opMode & 0x03) != 0) {
        return true;
    }
    // Fallback for the window before OPMODE has settled. This used to test
    // register 0x11 >= 3 V, which is ALWAYS true on this board - so the whole
    // function always returned true and a detach could never be detected at
    // all. The measured rail is the only usable substitute; with no
    // measurement, OPMODE alone decides.
    return _railValid && (_railVoltageV >= kRailValidMinV);
}

void PDManager::update() {
    _justAttached  = false;
    _justDetached  = false;
    _voltageJumped = false;

    if (_shutdownLatched) {
        return;
    }

    const bool intLevel = (_intPin >= 0) && (digitalRead(_intPin) == HIGH);
    uint8_t status = 0;
    if (intLevel) {
        status = pollStatus();
    } else {
        // Fallback: poll STATUS periodically even without an INT edge, so we
        // never miss STARTED/NEWPDO/READY if the INT line is not wired or the
        // level-triggered pulse is missed. The datasheet allows handling INT
        // or polling STATUS.
        if (millis() - _lastStatusPollMs >= kStatusPollMs) {
            _lastStatusPollMs = millis();
            status = pollStatus();
        }
    }

    // OPMODE transitions, logged with timestamps. OPMODE is the only window
    // into the chip's source classification (CCFLIP / PDMOD / LGCYMOD), and
    // WHEN those bits move relative to STARTED/READY is the evidence that
    // separates "the source never sent capabilities" from "the chip was deaf
    // to them": per datasheet 7.5, a failing DN (moisture) impedance check
    // makes the chip not acknowledge Source_Capability messages at all. A
    // failed I2C read also shows up here as a transition to 0x00, which makes
    // a wedged bus visible instead of silent.
    const uint8_t opMode = _pd.readOpMode();
    if (opMode != _lastOpMode) {
        DiagLog::event("opmode 0x%02X -> 0x%02X%s%s%s",
                       _lastOpMode, opMode,
                       (opMode & 0x80) ? " CCFLIP" : "",
                       (opMode & 0x02) ? " PDMOD" : "",
                       (opMode & 0x01) ? " LGCYMOD" : "");
        _lastOpMode = opMode;
    }

    // Handle physical disconnection. Require a sustained absence (not a count
    // of calls) so a transient OPMODE/VOUT dip during a voltage transition
    // (5V -> 20V) does not falsely disconnect and drop VBUS, which would
    // brownout a PD-powered ESP.
    if (_state != PDNegotiationState::Disconnected) {
        if (!isSourcePhysicallyPresent()) {
            if (_absentSinceMs == 0) {
                _absentSinceMs = millis();
            }
            if (millis() - _absentSinceMs >= kAbsentConfirmMs) {
                DiagLog::event("DETACH from state %u (rail=%.2fV)",
                               static_cast<unsigned>(_state), _railVoltageV);
                if (_state == PDNegotiationState::Ready) {
                    _justDetached = true;
                }
                _state          = PDNegotiationState::Disconnected;
                _activePdoIndex = 0;
                _voltage        = 0.0f;
                _current        = 0.0f;
                _absentSinceMs  = 0;
                // The chip is on its own power domain; a fresh attach must
                // reconfigure it from scratch.
                _configured           = false;
                _contractLostSinceMs  = 0;
                _recoveryAttempts     = 0;
                _recoveryExhausted    = false;
                _lastPdoPollMs        = 0;
                return;
            }
        } else {
            _absentSinceMs = 0;
        }
    }

    // STARTED after we were already past Disconnected means the AP33772S itself
    // restarted (its own POR, or a PD hard reset). Every configurable register
    // is back at its power-on default: VOUTCTL returns to Auto, UVP_EN to 1,
    // MASK to 0x03. Without re-applying them the chip runs in a configuration
    // this firmware never chose, and the old one-shot _configured latch made
    // that permanent.
    if ((status & STARTED_MSK) && _state != PDNegotiationState::Disconnected) {
        DiagLog::event("CHIP RESTARTED (STARTED while in state %u)",
                       static_cast<unsigned>(_state));
        _configured = false;
        applyStartupConfig();
        if (_state == PDNegotiationState::Ready) {
            attemptRecovery("chip restart");
        } else if (_state == PDNegotiationState::AttachSettle) {
            _stateEnteredAt = millis();
            _nextRequestAllowedMs = 0;
            _lastPdoPollMs = 0;
        }
    }

    // A protection trip opens the VOUT MOS internally and leaves it open. Per
    // the EVB guide 6.3/7.1-7.4 the host must load a new PD_REQMSG to resume;
    // nothing recovers by itself.
    if ((status & kFaultMask) && _state == PDNegotiationState::Ready) {
        attemptRecovery("protection trip");
    }

    switch (_state) {
        case PDNegotiationState::Disconnected: {
            const bool chipStarted = (status & STARTED_MSK) != 0;
            const bool sourceDetected = (opMode & 0x03) != 0;

            if (chipStarted || sourceDetected) {
                applyStartupConfig(); // config writes must follow STARTED=1
                DiagLog::event("attach: rail=%.2fV opmode=0x%02X%s",
                               _railVoltageV, opMode,
                               chipStarted ? " (STARTED)" : "");
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
                _lastPdoPollMs  = 0;
                _nextRequestAllowedMs = 0;
            }
            break;
        }

        case PDNegotiationState::AttachSettle: {
            if (_nextRequestAllowedMs != 0 &&
                static_cast<int32_t>(millis() - _nextRequestAllowedMs) < 0) {
                break; // still backing off from a rejected request
            }

            const uint32_t now = millis();

            // Poll the PDO table first: if capabilities have arrived, request
            // the best PDO and leave AttachSettle immediately.
            const bool pollDue = (status & NEWPDO_MSK) ||
                                 (_lastPdoPollMs == 0) ||
                                 (now - _lastPdoPollMs >= kAttachSettlePollMs);
            if (pollDue) {
                _lastPdoPollMs = now;
                _pd.refreshPdoTable();

                if (requestBestPdo()) {
                    DiagLog::event("request pdo%u %.1fV %.2fA", _activePdoIndex, _voltage, _current);
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = now;
                    break;
                }
            }

            // Repeated re-request: fire a raw PD_REQMSG every few hundred ms
            // even with no cached capabilities. The AP33772S has no direct
            // Get_Source_Cap command (PD_CMDMSG only exposes HRST/DRSWP), but
            // writing a request when no caps are cached may prompt the chip to
            // issue Get_Source_Cap to the source to obtain them.
            if (now - _lastCapsReqMs >= kCapsReRequestMs) {
                _lastCapsReqMs = now;
                _pd.requestPdoRaw(1, 0); // request PDO 1 @ 5V (CURRENT_SEL 0)
                DiagLog::event("caps re-request (rail=%.2fV opmode=0x%02X)",
                               _railVoltageV, opMode);
            }
            break;
        }

        case PDNegotiationState::Requesting: {
            // Guide 8.3, step 4: "After writing to the PD_REQMSG register, the
            // host MCU should wait for the STATUS.READY flag to be set high and
            // then read the PD_MSGRLT register to obtain the negotiation
            // result." The old condition OR'd three weak signals together and
            // every one of them can be true without a new contract:
            //  - STATUS.READY means "ready to receive an I2C request", not
            //    "the request was accepted";
            //  - PD_MSGRLT is RO and sticky, so it still holds the 1 from the
            //    PREVIOUS negotiation;
            //  - VREQ is only updated on success, so it is likewise stale, and
            //    _voltage was already assigned from the REQUEST in
            //    requestBestPdo() before the source had agreed to anything.
            const uint8_t msgResult   = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
            const float activeVoltage = _pd.readVREQ() / 1000.0f;
            const bool voltageMatched = (_voltage > 0.0f) && (fabsf(activeVoltage - _voltage) <= 0.5f);
            // The physical outcome, which no stale register can fake - measured
            // by the INA238, not by register 0x11 (see getVoutVoltageV()).
            //
            // With no rail measurement this cannot be checked, and it must then
            // PASS, not fail. Requiring it unconditionally is what produced the
            // infinite request loop: 0x11 reads ~5 V always, so the gate was
            // permanently shut for every contract above 6.6 V and the state
            // machine could never leave Requesting.
            const bool  railArrived   = !_railValid ||
                ((_voltage > 0.0f) && (_railVoltageV >= _voltage * kContractLiveFraction));
            const bool  msgSuccess    = (msgResult == 1);

            // An explicit rejection is not "still busy": stop waiting out the
            // full timeout and say which one it was.
            if (msgResult >= 2) {
                static const char *kMsgResultText[] = {
                    "busy", "success", "invalid command/argument",
                    "not supported / rejected", "no GoodCRC"};
                DiagLog::event("REQUEST REJECTED msgrlt=%u (%s)", msgResult,
                               (msgResult <= 4) ? kMsgResultText[msgResult] : "reserved");
                _voltage        = 0.0f;
                _current        = 0.0f;
                _activePdoIndex = 0;
                // Back off before re-requesting: AttachSettle would otherwise
                // re-send the identical PDO every 300 ms forever.
                _nextRequestAllowedMs = millis() + kRecoveryBackoffMs;
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
                break;
            }

            if (msgSuccess && voltageMatched && railArrived) {
                _state = PDNegotiationState::Ready;
                // Publish what was GRANTED, not what was asked for. These were
                // assigned from the request in requestBestPdo() before the
                // source had agreed to anything, so the screen and the power
                // budget could both describe a contract that never existed.
                const int grantedVreqMv = _pd.readVREQ();
                const int grantedIreqMa = _pd.readIREQ();
                if (grantedVreqMv > 0) { _voltage = grantedVreqMv / 1000.0f; }
                if (grantedIreqMa > 0) { _current = grantedIreqMa / 1000.0f; }
                _justAttached        = true;
                _contractLostSinceMs = 0;
                _recoveryAttempts    = 0;
                _recoveryExhausted   = false;
                DiagLog::event("READY %.1fV/%.2fA rail=%.2fV", _voltage, _current, _railVoltageV);
                Serial.printf("[PD] Contract active: %.1fV %.2fA %.1fW\n",
                              _voltage, _current, _voltage * _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                DiagLog::event("request TIMEOUT msgrlt=%u rail=%.2fV VREQ=%.2fV",
                               msgResult, _railVoltageV, activeVoltage);
                Serial.println("[PD] Contract request timed out; retrying...");
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;
        }

        case PDNegotiationState::Ready: {
            // Contract liveness. This is the check that was missing entirely:
            // the old code only ever compared the source's ADVERTISED profile
            // list against the contract, so a source that fell back to vSafe5V
            // while still advertising 20 V left the firmware sitting on a dead
            // contract forever - reporting 20 V on screen, "(e)" for the output,
            // and no fault anywhere.
            //
            // Measured by the INA238. Against register 0x11 this test was TRUE
            // on the very first tick of every contract above 6.6 V, so had the
            // acceptance gate ever let a contract through, this would have torn
            // it down 300 ms later and burned all 5 recovery attempts. With no
            // rail measurement the check is skipped: a missing sensor must not
            // be able to destroy a working contract.
            if (_railValid && _voltage > 0.0f &&
                _railVoltageV < _voltage * kContractLiveFraction) {
                if (_contractLostSinceMs == 0) {
                    _contractLostSinceMs = millis();
                }
                if (millis() - _contractLostSinceMs >= kContractLostMs) {
                    attemptRecovery("contract lost");
                    break;
                }
            } else {
                _contractLostSinceMs = 0;
                _recoveryAttempts    = 0;
                _recoveryExhausted   = false;
            }

            const bool dueForRecheck = (status & NEWPDO_MSK) ||
                (millis() - _stateEnteredAt >= kProfileRecheckMs);
            if (!dueForRecheck) {
                break;
            }
            _stateEnteredAt = millis();

            // Compare-and-repair. Re-applying config on a STARTED edge only
            // helps if we actually saw that edge; a config write can also fail
            // silently on a bus hiccup. Reading the registers back is the only
            // way to know the chip is running the configuration we chose.
            uint8_t cfg = 0, msk = 0;
            if (_pd.readRawByte(CMD_CONFIG, cfg) && _pd.readRawByte(CMD_MASK, msk)) {
                const uint8_t wantMask = STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask;
                if ((cfg & UVP_EN) || msk != wantMask) {
                    DiagLog::event("CONFIG DRIFT cfg=0x%02X mask=0x%02X; re-applying", cfg, msk);
                    _configured = false;
                    applyStartupConfig();
                }
            }

            _pd.refreshPdoTable();

            const uint8_t bestPdo = findBestFixedPdo();
            // Re-negotiate only when the best available VOLTAGE actually
            // differs from the active contract. Comparing PDO indices is
            // wrong: a pending NEWPDO event (e.g. from the initial
            // negotiation) can make findBestFixedPdo() return a different
            // index with the SAME voltage, causing a needless re-negotiation
            // that drops VBUS and resets a PD-powered ESP.
            const float bestVoltageV = (bestPdo != 0) ? _pd.getPdoVoltageV(bestPdo) : 0.0f;
            if (bestPdo != 0 && fabsf(bestVoltageV - _voltage) > 0.5f) {
                DiagLog::event("profile change %.1fV -> %.1fV", _voltage, bestVoltageV);
                Serial.printf("[PD] Source profile changed (%.1fV -> %.1fV); re-negotiating.\n",
                              _voltage, bestVoltageV);
                _voltageJumped = true;
                // Do NOT force VOUT off here: it stays on and follows VBUS
                // down to 5V and back up during re-negotiation (like the old
                // HUSB238 board). Forcing it off caused the output to stay
                // off after re-negotiation.
                if (requestBestPdo()) {
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                }
            }
            break;
        }
    }
}

// The datasheet-mandated recovery: after a protection trip, a source-side hard
// reset or a chip restart, the host has to send a fresh PD_REQMSG. Rate-limited
// and bounded so a source that will not co-operate cannot be hammered.
bool PDManager::attemptRecovery(const char *reason) {
    if (_recoveryExhausted) {
        return false;
    }
    if (_lastRecoveryMs != 0 && millis() - _lastRecoveryMs < kRecoveryBackoffMs) {
        return false;
    }

    _lastRecoveryMs = millis();
    ++_recoveryAttempts;

    DiagLog::event("RECOVER %u/%u: %s (rail=%.2fV want %.1fV)",
                   _recoveryAttempts, kMaxRecoveryAttempts, reason, _railVoltageV, _voltage);

    if (_recoveryAttempts > kMaxRecoveryAttempts) {
        _recoveryExhausted = true;
        DiagLog::event("RECOVERY EXHAUSTED after %u tries", kMaxRecoveryAttempts);
        Serial.printf("[PD] Recovery failed %u times (%s); giving up until re-attach.\n",
                      kMaxRecoveryAttempts, reason);
        return false;
    }

    // The contract is gone, so nothing downstream may keep acting on it.
    _voltageJumped = true;
    _contractLostSinceMs = 0;

    _pd.refreshPdoTable();
    if (!requestBestPdo()) {
        DiagLog::event("RECOVER: no eligible PDO; back to settle");
        _state          = PDNegotiationState::AttachSettle;
        _stateEnteredAt = millis();
        _lastPdoPollMs  = 0;
        return false;
    }

    _state          = PDNegotiationState::Requesting;
    _stateEnteredAt = millis();
    return true;
}

bool PDManager::requestBestPdo() {
    const uint8_t bestPdo = findBestFixedPdo();
    if (bestPdo == 0) {
        return false;
    }

    const float voltage    = _pd.getPdoVoltageV(bestPdo);
    const float maxCurrent = _pd.getPdoMaxCurrentA(bestPdo);

    if (!_pd.requestFixedPDO(bestPdo)) {
        return false;
    }

    _activePdoIndex = bestPdo;
    _voltage        = voltage;
    _current        = maxCurrent;
    return true;
}

uint8_t PDManager::findBestFixedPdo() const {
    uint8_t bestPdo     = 0; // 1-based PDO number, 0 = none found
    float   bestVoltageV = -1.0f;

    for (int pdo = 1; pdo <= MAX_PDO_ENTRIES; ++pdo) {
        const float voltageV = _pd.getPdoVoltageV(pdo);
        if (voltageV <= 0.0f || voltageV > kMaxRequestVoltageV) {
            continue;
        }
        // Skip the CURRENT_MAX code-0 bucket ("less than 1.24 A"), for which
        // getPdoMaxCurrentA() now reports 0 A. Selecting such a PDO would give
        // a zero power budget, so the output would never enable - and it would
        // also pin the chip's OCP threshold to that same small bucket.
        // NOTE: selection is still "highest usable voltage", not "most power".
        // Changing that policy would move the operating point on the PSUs that
        // already work, so it is left as a deliberate decision for you.
        if (_pd.getPdoMaxCurrentA(pdo) < kMinUsablePdoCurrentA) {
            continue;
        }
        if (voltageV > bestVoltageV) {
            bestVoltageV = voltageV;
            bestPdo      = static_cast<uint8_t>(pdo);
        }
    }
    return bestPdo;
}

bool PDManager::isAttached() const {
    return _state != PDNegotiationState::Disconnected;
}

bool PDManager::isReady() const {
    return _state == PDNegotiationState::Ready;
}

bool PDManager::isAttachRetrying() const {
    return _state == PDNegotiationState::AttachSettle;
}

bool PDManager::justAttached() const {
    return _justAttached;
}

bool PDManager::justDetached() const {
    return _justDetached;
}

bool PDManager::voltageJumped() const {
    return _voltageJumped;
}

float PDManager::getVoltage() const {
    return _voltage;
}

float PDManager::getCurrent() const {
    return _current;
}

float PDManager::getVoutVoltageV() {
    return _pd.readVoltage() / 1000.0f;
}

void PDManager::shutdown() {
    if (_shutdownLatched) {
        return;
    }

    _shutdownLatched = true;
    DiagLog::event("PD SHUTDOWN latched (rail=%.2fV)", _railVoltageV);
    Serial.println("[PD] Shutdown: switching VBUS off");
}
