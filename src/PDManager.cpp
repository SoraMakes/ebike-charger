#include "PDManager.h"

#include "DiagLog.h"

#include <math.h>
#include <string.h>

// INT-pin mask.
static constexpr uint8_t kFaultMask = UVP_MSK | OVP_MSK | OCP_MSK | OTP_MSK;
static constexpr uint32_t kNegotiationTimeoutMs = 2000;
static constexpr uint32_t kProfileRecheckMs = 500;
static constexpr uint32_t kStatusPollMs = 500;
// Dead below 75% of contract.
static constexpr float kContractLiveFraction = 0.75f;
// Dead-declare dwell.
static constexpr uint32_t kContractLostMs = 300;
static constexpr uint32_t kRecoveryBackoffMs = 2000;

bool PDManager::begin(TwoWire &wire, int8_t intPin) {
    _intPin = intPin;
    // INT idles low; floating fakes interrupts.
    pinMode(_intPin, INPUT_PULLDOWN);
    bool ok = _pd.begin(wire);
    if (ok) {
        _pd.setOutput(0);
    }
    return ok;
}

// Once after STARTED.
void PDManager::applyStartupConfig() {
    if (_configured) {
        return;
    }
    // False UVP on RotoPD Pro.
    _pd.clearConfig(AP33772_CONFIG::UVP_EN);
    _pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask);
    _configured = true;
}

void PDManager::setMeasuredInputVoltage(float volts) {
    _inputVoltageV = volts;
    _inputValid = isfinite(volts) && (volts >= kInputValidMinV);
}

// Read-clear STATUS latch.
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

// Latched protections, e.g. OCP OVP OTP.
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

void PDManager::update() {
    _justAttached = false;
    _voltageJumped = false;
    if (_shutdownLatched) {
        _pd.setOutput(0);
        return;
    }

    // Fast INT plus fallback poll.
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

    // VBUS loss reboots; sags use contract-lost path.
    // Chip restart: re-apply config.
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

    // Trip leaves VOUT open until re-request.
    if ((status & kFaultMask) && _state == PDNegotiationState::Ready) {
        attemptRecovery("protection trip");
    }

    switch (_state) {
        case PDNegotiationState::Disconnected: {
            const bool chipStarted = (status & STARTED_MSK) != 0;
            const bool sourceDetected = (opMode & kOpModeSourceMask) != 0;
            if (chipStarted || sourceDetected) {
                applyStartupConfig();
                _pd.setOutput(1);
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
            // Msg success + VREQ match + input arrival.
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
                // Granted voltage/current.
                const int grantedVreqMv = _pd.readVREQ();
                const int grantedIreqMa = _pd.readIREQ();
                if (grantedVreqMv > 0) { _voltage = grantedVreqMv / 1000.0f; }
                if (grantedIreqMa > 0) { _current = grantedIreqMa / 1000.0f; }
                _justAttached = true;
                _contractLostSinceMs = 0;
                _pd.setOutput(1);
                DiagLog::event("contract %.0fV %.1fA", _voltage, _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                _state = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;
        }

        case PDNegotiationState::Ready: {
            // Dead contract via input volts; fail-open.
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
            }
            const bool dueForRecheck = (status & NEWPDO_MSK) || (millis() - _stateEnteredAt >= kProfileRecheckMs);
            if (!dueForRecheck) {
                break;
            }
            _stateEnteredAt = millis();
            _pd.refreshPdoTable();
            const uint8_t bestPdo = findBestFixedPdo();
            // Same voltage must not renegotiate.
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

// Re-request after trip/restart/loss.
bool PDManager::attemptRecovery(const char *reason) {
    (void)reason;
    if (_lastRecoveryMs != 0 && millis() - _lastRecoveryMs < kRecoveryBackoffMs) {
        return false;
    }
    _lastRecoveryMs = millis();
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
        // Unusable low-current bucket.
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

bool PDManager::isReady() const {
    return !_shutdownLatched && _state == PDNegotiationState::Ready;
}

bool PDManager::justAttached() const {
    return _justAttached;
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

void PDManager::shutdown(const char *reason) {
    if (_shutdownLatched) {
        return;
    }
    _shutdownLatched = true;
    strncpy(_shutdownReason, reason ? reason : "unknown", sizeof(_shutdownReason) - 1);
    _shutdownReason[sizeof(_shutdownReason) - 1] = '\0';
    _pd.setOutput(0);
    DiagLog::event("safety %s", _shutdownReason);
}
