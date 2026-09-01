#include "PDManager.h"

#include "DiagLog.h"

#include <math.h>
#include <string.h>

// Every AP33772S protection bit. STATUS carries these whether or not they are
// unmasked; the mask only gates the physical INT pin.
static constexpr uint8_t kFaultMask = UVP_MSK | OVP_MSK | OCP_MSK | OTP_MSK;

// Delay after attach detection before the controller's profile list is trusted.
static constexpr uint32_t kAttachSettleMs       = 300;
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
    // TODO: i dont want to disable them!
    // TODO: i want actually usefull setOutput handling
    // UVP must be disabled for the RotoPD Pro (avoids UVP hard resets).
    // The other AP33772S protections stay at their power-on defaults.
    _pd.clearConfig(AP33772_CONFIG::UVP_EN);
    // Unmask the protection bits too: MASK only gates the INT pin, and leaving
    // UVP/OVP/OCP/OTP masked meant a protection trip never raised INT and was
    // only ever seen (and destroyed) by the periodic STATUS poll.
    _pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask);
    // Turn VOUT ON now, at 5V, BEFORE the 20V request (matches the official
    // CentyLab example flow: setOutput(1) right after init). The VR input caps
    // charge gently at 5V, then VOUT follows VBUS as it ramps to 20V. Turning
    // VOUT on AFTER the 20V contract (the old approach) suddenly switched 20V
    // onto empty caps -> inrush -> crash. Auto control (VOUTCTL=0) did not
    // turn VOUT on reliably, so force-ON early is the working approach.
    _pd.setOutput(1);
    _configured = true;
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
                               float outputVoltageV, int digipotStep) {
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

    // Classify VOUT into three bands and log the transitions. This is the
    // measurement that separates the two candidate failures: band 0 means the
    // VOUT MOS opened, band 1 means the contract collapsed back to ~5V while
    // the MOS stayed closed. Both look identical on the "(e)" display flag.
    const uint8_t band = (voutMv < 1000) ? 0 : (voutMv < 7000 ? 1 : 2);
    if (band != _lastVoutBand) {
        static const char *kBandNames[] = {"OFF(<1V)", "LOW(~5V)", "HIGH"};
        DiagLog::event("VOUT %s -> %s (%.2fV) contract=%.1fV",
                       (_lastVoutBand <= 2) ? kBandNames[_lastVoutBand] : "?",
                       kBandNames[band], voutMv / 1000.0f, _voltage);
        _lastVoutBand = band;
    }

    Serial.printf(
        "[PDDIAG] st=%s contract=%.1fV/%.2fA pdo=%u | VOUT=%.2fV VREQ=%.2fV "
        "IREQ=%.2fA Iout=%.2fA Tchip=%dC | status=0x%02X sticky=0x%02X%s%s "
        "msgrlt=%u opmode=0x%02X cfg=0x%02X mask=0x%02X sys=0x%02X voutctl=%u | "
        "ina=%.2fV/%.2fA out=%.2fV pot=%d | boot#%u %s\n",
        kStateNames[static_cast<uint8_t>(_state)],
        _voltage, _current, _activePdoIndex,
        voutMv / 1000.0f, vreqMv / 1000.0f, ireqMa / 1000.0f, ioutMa / 1000.0f, chipTmp,
        _statusLast, _statusSticky,
        hasChipFault() ? " FAULT:" : "", getFaultText(),
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
    // Fallback: VOUT >= 3.0V. Note register 0x11 is VOUT (post-MOS), not VBUS,
    // so this reads 0 whenever the MOS is open even though the source is still
    // attached. OPMODE above is the authoritative attach test; this only covers
    // the window before OPMODE has settled.
    return (_pd.readVoltage() >= 3000);
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
                DiagLog::event("DETACH from state %u (VOUT=%.2fV)",
                               static_cast<unsigned>(_state), _pd.readVoltage() / 1000.0f);
                if (_state == PDNegotiationState::Ready) {
                    _justDetached = true;
                }
                _pd.setOutput(0); // force-OFF
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
        }
    }

    // A protection trip opens the VOUT MOS internally and leaves it open. Per
    // the EVB guide 6.3/7.1-7.4 the host must load a new PD_REQMSG to resume;
    // nothing recovers by itself.
    if ((status & kFaultMask) && _state == PDNegotiationState::Ready) {
        attemptRecovery("protection trip");
    }

    switch (_state) {
        case PDNegotiationState::Disconnected:
            if ((status & STARTED_MSK) || isSourcePhysicallyPresent()) {
                applyStartupConfig(); // config writes must follow STARTED=1
                DiagLog::event("attach: VOUT=%.2fV opmode=0x%02X",
                               _pd.readVoltage() / 1000.0f, _pd.readOpMode());
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;

        case PDNegotiationState::AttachSettle:
            if (_nextRequestAllowedMs != 0 &&
                static_cast<int32_t>(millis() - _nextRequestAllowedMs) < 0) {
                break; // still backing off from a rejected request
            }
            if ((status & NEWPDO_MSK) || (millis() - _stateEnteredAt >= kAttachSettleMs)) {
                _nextRequestAllowedMs = 0;
                _pd.refreshPdoTable();

                if (requestBestPdo()) {
                    DiagLog::event("request pdo%u %.1fV %.2fA", _activePdoIndex, _voltage, _current);
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                } else {
                    // No eligible fixed PDO yet. Re-stamp so the retry stays
                    // rate-limited to kAttachSettleMs: without this the elapsed
                    // test above remains permanently true and refreshPdoTable()
                    // - a 26-byte I2C burst - would run on every tick, i.e. at
                    // 20 Hz since the task moved to 50 ms.
                    _stateEnteredAt = millis();
                }
            }
            break;

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
            // The physical outcome, which no stale register can fake.
            const float voutV         = _pd.readVoltage() / 1000.0f;
            const bool  voutArrived   = (_voltage > 0.0f) && (voutV >= _voltage * kContractLiveFraction);
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

            if (msgSuccess && voltageMatched && voutArrived) {
                _state = PDNegotiationState::Ready;
                // Ensure VOUT is ON. It was turned on at 5V in
                // applyStartupConfig(); this also re-enables it after a
                // re-negotiation (the recheck no longer forces VOUT off).
                _pd.setOutput(1);
                _justAttached        = true;
                _contractLostSinceMs = 0;
                _recoveryAttempts    = 0;
                _recoveryExhausted   = false;
                DiagLog::event("READY %.1fV VOUT=%.2fV VREQ=%.2fV", _voltage, voutV, activeVoltage);
                Serial.printf("[PD] Contract active: %.1fV %.2fA %.1fW\n",
                              _voltage, _current, _voltage * _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                DiagLog::event("request TIMEOUT msgrlt=%u VOUT=%.2fV VREQ=%.2fV",
                               msgResult, voutV, activeVoltage);
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
            const float voutV = _pd.readVoltage() / 1000.0f;
            if (_voltage > 0.0f && voutV < _voltage * kContractLiveFraction) {
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

    const float voutV = _pd.readVoltage() / 1000.0f;
    DiagLog::event("RECOVER %u/%u: %s (VOUT=%.2fV want %.1fV)",
                   _recoveryAttempts, kMaxRecoveryAttempts, reason, voutV, _voltage);

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

float PDManager::getWattage() const {
    return _voltage * _current;
}

bool PDManager::isOutputEnabled() {
    // VOUT is either ~0V (MOS off) or >= 5V (MOS on, following VBUS), so a
    // 1V threshold cleanly separates the two states. Reading the measured
    // voltage is the only reliable way: the chip's own protections turn the
    // MOS off internally without updating the SYSTEM register.
    return _pd.readVoltage() >= 1000;
}

float PDManager::getVoutVoltageV() {
    return _pd.readVoltage() / 1000.0f;
}

void PDManager::shutdown() {
    if (_shutdownLatched) {
        return;
    }

    _shutdownLatched = true;
    DiagLog::event("PD SHUTDOWN latched (VOUT=%.2fV)", _pd.readVoltage() / 1000.0f);
    Serial.println("[PD] Shutdown: switching VBUS off");
    _pd.setOutput(0); // force-OFF
}
