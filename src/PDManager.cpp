#include "PDManager.h"

#include "DiagLog.h"

#include <math.h>
#include <string.h>

// Protection bits (mask gates INT pin only).
static constexpr uint8_t kFaultMask = UVP_MSK | OVP_MSK | OCP_MSK | OTP_MSK;
static constexpr uint32_t kNegotiationTimeoutMs = 2000;
static constexpr uint32_t kProfileRecheckMs = 500;
static constexpr uint32_t kStatusPollMs = 500;
static constexpr uint32_t kAbsentConfirmMs = 500;
// Contract dead when input below 75% of contract (20V->5V is 25%).
static constexpr float kContractLiveFraction = 0.75f;
// Low-input dwell before contract declared dead.
static constexpr uint32_t kContractLostMs = 300;
static constexpr uint32_t kRecoveryBackoffMs = 2000;
static constexpr uint8_t kMaxRecoveryAttempts = 5;

bool PDManager::begin(TwoWire &wire, int8_t intPin) {
    _intPin = intPin;
    // INT idles low; floating pin would fake interrupts and eat STATUS.
    pinMode(_intPin, INPUT_PULLDOWN);
    return _pd.begin(wire);
}

// Config written once after STARTED, before first negotiation.
void PDManager::applyStartupConfig() {
    if (_configured) {
        return;
    }
    // UVP trips falsely on RotoPD Pro.
    _pd.clearConfig(AP33772_CONFIG::UVP_EN);
    // Unmask faults to INT pin.
    _pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask);
    _configured = true;
}

void PDManager::setMeasuredInputVoltage(float volts) {
    _inputVoltageV = volts;
    _inputValid = isfinite(volts) && (volts >= kInputValidMinV);
}

// STATUS is read-clear: accumulate every bit into the sticky latch.
uint8_t PDManager::pollStatus() {
    const uint8_t status = _pd.readStatus();
    if (status == 0) {
        return 0;
    }
    const uint8_t freshFaults = static_cast<uint8_t>(status & kFaultMask & ~_statusSticky);
    _statusSticky |= status;
    if (freshFaults != 0) {
        DiagLog::event("chip fault %s", getFaultText());
    }
    return status;
}

// Returns e.g. "OCP " or "OVP OTP ": latched AP33772S protections.
// UVP=input undervoltage, OVP=input overvoltage, OCP=input overcurrent,
// OTP=chip overtemperature. Latched until re-attach; drives display/log.
const char *PDManager::getFaultText() const {
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

// Compact status line (crucial fields only).
void PDManager::logDiagnostics(float inaVoltageV, float inaCurrentA,
                               float outputVoltageV, int digipotStep,
                               const char *inaAlertText) {
    static const char *kStateNames[] = {"DISC", "SETTLE", "REQ", "READY"};
    const uint8_t opMode = _pd.readOpMode();
    const uint8_t msgRlt = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
    const bool hasInaAlert = (inaAlertText != nullptr && inaAlertText[0] != '\0');
    Serial.printf("[PD] st=%s contract=%.1fV/%.2fA opmode=0x%02X msgrlt=%u sticky=0x%02X%s%s%s%s ina=%.2fV/%.2fA out=%.2fV pot=%d\n",
        kStateNames[static_cast<uint8_t>(_state)],
        _voltage, _current,
        opMode, msgRlt, _statusSticky,
        hasChipFault() ? " FAULT:" : "", getFaultText(),
        hasInaAlert ? " INA:" : "", hasInaAlert ? inaAlertText : "",
        inaVoltageV, inaCurrentA, outputVoltageV, digipotStep);
}

bool PDManager::isSourcePhysicallyPresent() {
    if ((_pd.readOpMode() & kOpModeSourceMask) != 0) {
        return true;
    }
    // OPMODE unset before settle; fall back to PD input volts.
    return _inputValid && (_inputVoltageV >= kInputValidMinV);
}

void PDManager::update() {
    _justAttached = false;
    _justDetached = false;
    _voltageJumped = false;
    if (_shutdownLatched) {
        return;
    }

    // INT fast path plus slow fallback poll (missed/short pulse safety net).
    uint8_t status = 0;
    if (digitalRead(_intPin) == HIGH) {
        status = pollStatus();
    } else if (millis() - _lastStatusPollMs >= kStatusPollMs) {
        _lastStatusPollMs = millis();
        status = pollStatus();
    }

    const uint8_t opMode = _pd.readOpMode();
    if (opMode != _lastOpMode) {
        DiagLog::event("opmode 0x%02X", opMode);
        _lastOpMode = opMode;
    }
    if (status != _lastStatus) {
        DiagLog::event("status 0x%02X", status);
        _lastStatus = status;
    }

    // Sustained absence only; ignore dips during voltage transitions.
    if (_state != PDNegotiationState::Disconnected) {
        if (!isSourcePhysicallyPresent()) {
            if (_absentSinceMs == 0) {
                _absentSinceMs = millis();
            }
            if (millis() - _absentSinceMs >= kAbsentConfirmMs) {
                if (_state == PDNegotiationState::Ready) {
                    _justDetached = true;
                }
                _state = PDNegotiationState::Disconnected;
                _voltage = 0.0f;
                _current = 0.0f;
                _absentSinceMs = 0;
                _configured = false;
                _contractLostSinceMs = 0;
                _recoveryAttempts = 0;
                _recoveryExhausted = false;
                _lastPdoPollMs = 0;
                return;
            }
        } else {
            _absentSinceMs = 0;
        }
    }

    // Chip restarted underneath us: re-apply config, recover contract.
    if ((status & STARTED_MSK) && _state != PDNegotiationState::Disconnected) {
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

    // Protection trip leaves VOUT MOS open until fresh PD_REQMSG.
    if ((status & kFaultMask) && _state == PDNegotiationState::Ready) {
        attemptRecovery("protection trip");
    }

    switch (_state) {
        case PDNegotiationState::Disconnected: {
            const bool chipStarted = (status & STARTED_MSK) != 0;
            const bool sourceDetected = (opMode & kOpModeSourceMask) != 0;
            if (chipStarted || sourceDetected) {
                applyStartupConfig();
                _state = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
                _lastPdoPollMs = 0;
                _nextRequestAllowedMs = 0;
            }
            break;
        }

        case PDNegotiationState::AttachSettle: {
            if (_nextRequestAllowedMs != 0 &&
                static_cast<int32_t>(millis() - _nextRequestAllowedMs) < 0) {
                break;
            }
            const uint32_t now = millis();
            const bool pollDue = (status & NEWPDO_MSK) ||
                                 (_lastPdoPollMs == 0) ||
                                 (now - _lastPdoPollMs >= kAttachSettlePollMs);
            if (pollDue) {
                _lastPdoPollMs = now;
                _pd.refreshPdoTable();
                if (requestBestPdo()) {
                    _state = PDNegotiationState::Requesting;
                    _stateEnteredAt = now;
                    break;
                }
            }
            break;
        }

        case PDNegotiationState::Requesting: {
            // Accept only on msg success + VREQ match + input arrival.
            const uint8_t msgResult = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
            const float activeVoltage = _pd.readVREQ() / 1000.0f;
            const bool voltageMatched = _voltage > 0.0f && fabsf(activeVoltage - _voltage) <= kVoltageMatchV;
            const bool inputArrived = !_inputValid || (_voltage > 0.0f && _inputVoltageV >= _voltage * kContractLiveFraction);
            const bool msgSuccess = (msgResult == 1);
            if (msgResult >= 2) {
                _voltage = 0.0f;
                _current = 0.0f;
                _nextRequestAllowedMs = millis() + kRecoveryBackoffMs;
                _state = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
                break;
            }
            if (msgSuccess && voltageMatched && inputArrived) {
                _state = PDNegotiationState::Ready;
                // Republish granted (not requested) voltage/current.
                const int grantedVreqMv = _pd.readVREQ();
                const int grantedIreqMa = _pd.readIREQ();
                if (grantedVreqMv > 0) { _voltage = grantedVreqMv / 1000.0f; }
                if (grantedIreqMa > 0) { _current = grantedIreqMa / 1000.0f; }
                _justAttached = true;
                _contractLostSinceMs = 0;
                _recoveryAttempts = 0;
                _recoveryExhausted = false;
                DiagLog::event("contract %.0fV %.1fA", _voltage, _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                _state = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;
        }

        case PDNegotiationState::Ready: {
            // Dead contract via PD input volts (no sensor: skip, don't fail).
            if (_inputValid && _voltage > 0.0f && _inputVoltageV < _voltage * kContractLiveFraction) {
                if (_contractLostSinceMs == 0) {
                    _contractLostSinceMs = millis();
                }
                if (millis() - _contractLostSinceMs >= kContractLostMs) {
                    attemptRecovery("contract lost");
                    break;
                }
            } else {
                _contractLostSinceMs = 0;
                _recoveryAttempts = 0;
                _recoveryExhausted = false;
            }
            const bool dueForRecheck = (status & NEWPDO_MSK) || (millis() - _stateEnteredAt >= kProfileRecheckMs);
            if (!dueForRecheck) {
                break;
            }
            _stateEnteredAt = millis();
            _pd.refreshPdoTable();
            const uint8_t bestPdo = findBestFixedPdo();
            // Compare voltage, not index: same voltage must not renegotiate.
            const float bestVoltageV = (bestPdo != 0) ? _pd.getPdoVoltageV(bestPdo) : 0.0f;
            if (bestPdo != 0 && fabsf(bestVoltageV - _voltage) > kVoltageMatchV) {
                DiagLog::event("contract %.0fV->%.0fV", _voltage, bestVoltageV);
                _voltageJumped = true;
                if (requestBestPdo()) {
                    _state = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                }
            }
            break;
        }
    }
}

// Fresh PD_REQMSG after trip/restart/loss (bounded, rate-limited).
bool PDManager::attemptRecovery(const char *reason) {
    if (_recoveryExhausted) {
        return false;
    }
    if (_lastRecoveryMs != 0 && millis() - _lastRecoveryMs < kRecoveryBackoffMs) {
        return false;
    }
    _lastRecoveryMs = millis();
    ++_recoveryAttempts;
    if (_recoveryAttempts > kMaxRecoveryAttempts) {
        _recoveryExhausted = true;
        DiagLog::event("safety giveup %s", reason);
        return false;
    }
    _voltageJumped = true;
    _contractLostSinceMs = 0;
    _pd.refreshPdoTable();
    if (!requestBestPdo()) {
        _state = PDNegotiationState::AttachSettle;
        _stateEnteredAt = millis();
        _lastPdoPollMs = 0;
        return false;
    }
    _state = PDNegotiationState::Requesting;
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

    _voltage        = voltage;
    _current        = maxCurrent;
    return true;
}

uint8_t PDManager::findBestFixedPdo() const {
    uint8_t bestPdo = 0;
    float bestVoltageV = -1.0f;
    for (int pdo = 1; pdo <= MAX_PDO_ENTRIES; ++pdo) {
        const float voltageV = _pd.getPdoVoltageV(pdo);
        if (voltageV <= 0.0f || voltageV > kMaxRequestVoltageV) {
            continue;
        }
        // Skip "<1.24A" bucket: unusable and pins OCP too low.
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
    return !_shutdownLatched && _state != PDNegotiationState::Disconnected;
}

bool PDManager::isReady() const {
    return !_shutdownLatched && _state == PDNegotiationState::Ready;
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

void PDManager::shutdown() {
    if (_shutdownLatched) {
        return;
    }
    _shutdownLatched = true;
    _voltage = 0.0f;
    _current = 0.0f;
    _state = PDNegotiationState::Disconnected;
    DiagLog::event("safety pd off");
}
