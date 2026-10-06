#include "PDManager.h"

#include "DiagLog.h"

#include <math.h>
#include <string.h>

// STATUS bits for undervoltage, overvoltage, overcurrent, and overtemperature faults.
// These bits in the interrupt mask make those faults assert INT.
static constexpr uint8_t kFaultMask = UVP_MSK | OVP_MSK | OCP_MSK | OTP_MSK;
// Time allowed for a PD request to complete.
static constexpr uint32_t kNegotiationTimeoutMs = 2000;
// Interval between advertised PDO-table checks while Ready.
static constexpr uint32_t kProfileRecheckMs = 500;
// Fallback STATUS-read interval while the interrupt pin is inactive.
static constexpr uint32_t kStatusPollMs = 500;
// Require a sustained rail drop before requesting a new contract.
static constexpr uint32_t kContractLostMs = 300;
// Minimum delay between recovery attempts and after a rejected request.
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
    _contractChanged = false;
    if (_shutdownLatched) {
        _pd.setOutput(0);
        return;
    }

    uint8_t opMode = 0;
    const uint8_t status = pollInputs(opMode);
    handleChipRestart(status);
    handleProtectionTrip(status);

    switch (_state) {
        case PDNegotiationState::Disconnected: onDisconnected(status, opMode); break;
        case PDNegotiationState::AttachSettle: onAttachSettle(status); break;
        case PDNegotiationState::Requesting: onRequesting(); break;
        case PDNegotiationState::Ready: onReady(status); break;
    }
}

// Shared state entry.
void PDManager::enter(PDNegotiationState state) {
    _state = state;
    _stateEnteredAt = millis();
}

// Fast INT plus fallback poll; logs changes.
uint8_t PDManager::pollInputs(uint8_t &opMode) {
    uint8_t status = 0;
    if (digitalRead(_intPin) == HIGH) {
        status = pollStatus();
    } else if (millis() - _lastStatusPollMs >= kStatusPollMs) {
        _lastStatusPollMs = millis();
        status = pollStatus();
    }

    opMode = _pd.readOpMode();
    if (opMode != _lastOpMode) {
        DiagLog::event("opmode 0x%02X", opMode);
        _lastOpMode = opMode;
    }
    if (status != _lastStatus) {
        DiagLog::event("status 0x%02X", status);
        _lastStatus = status;
    }
    return status;
}

// VBUS loss reboots; sags use contract-lost path.
void PDManager::handleChipRestart(uint8_t status) {
    if (!(status & STARTED_MSK) || _state == PDNegotiationState::Disconnected) {
        return;
    }
    // Chip restart: re-apply config.
    _configured = false;
    applyStartupConfig();
    if (_state == PDNegotiationState::Ready) {
        attemptRecovery();
    } else if (_state == PDNegotiationState::AttachSettle) {
        enter(PDNegotiationState::AttachSettle);
        _nextRequestAllowedMs = 0;
        _lastPdoPollMs = 0;
    }
}

// Trip leaves VOUT open until re-request.
void PDManager::handleProtectionTrip(uint8_t status) {
    if ((status & kFaultMask) && _state == PDNegotiationState::Ready) {
        attemptRecovery();
    }
}

void PDManager::onDisconnected(uint8_t status, uint8_t opMode) {
    const bool chipStarted = (status & STARTED_MSK) != 0;
    const bool sourceDetected = (opMode & kOpModeSourceMask) != 0;
    if (!chipStarted && !sourceDetected) {
        return;
    }
    applyStartupConfig();
    _pd.setOutput(1);
    enter(PDNegotiationState::AttachSettle);
    _lastPdoPollMs = 0;
    _nextRequestAllowedMs = 0;
}

void PDManager::onAttachSettle(uint8_t status) {
    if (_nextRequestAllowedMs != 0 &&
        static_cast<int32_t>(millis() - _nextRequestAllowedMs) < 0) {
        return;
    }
    const uint32_t now = millis();
    const bool pollDue = (status & NEWPDO_MSK) ||
                         (_lastPdoPollMs == 0) ||
                         (now - _lastPdoPollMs >= kAttachSettlePollMs);
    if (!pollDue) {
        return;
    }
    _lastPdoPollMs = now;
    if (!_pd.refreshPdoTable()) {
        DiagLog::event("PDO refresh failed (attach)");
        return;
    }
    if (requestBestPdo()) {
        enter(PDNegotiationState::Requesting);
    }
}

void PDManager::onRequesting() {
    // Message success plus matching voltage, current, and measured input rail.
    const uint8_t msgResult = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
    const float activeVoltage = _pd.readVREQ() / 1000.0f;
    const bool voltageMatched = _voltage > 0.0f &&
        fabsf(activeVoltage - _voltage) <= _voltage * kVoltageToleranceFraction;
    const float activeCurrent = _pd.readIREQ() / 1000.0f;
    const bool currentMatched = _current > 0.0f && activeCurrent == _current;
    const float minimumInputVoltage = _voltage * (1.0f - kVoltageToleranceFraction);
    const bool inputArrived = !_inputValid || (_voltage > 0.0f && _inputVoltageV >= minimumInputVoltage);
    if (msgResult >= 2) {
        _voltage = 0.0f;
        _current = 0.0f;
        _nextRequestAllowedMs = millis() + kRecoveryBackoffMs;
        enter(PDNegotiationState::AttachSettle);
        return;
    }
    if (msgResult == 1 && voltageMatched && currentMatched && inputArrived) {
        enter(PDNegotiationState::Ready);
        // Granted voltage/current.
        const int grantedVreqMv = _pd.readVREQ();
        const int grantedIreqMa = _pd.readIREQ();
        if (grantedVreqMv > 0) { _voltage = grantedVreqMv / 1000.0f; }
        if (grantedIreqMa > 0) { _current = grantedIreqMa / 1000.0f; }
        _justAttached = true;
        _contractLostSinceMs = 0;
        _pd.setOutput(1);
        DiagLog::event("contract %.0fV %.1fA", _voltage, _current);
        return;
    }
    if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
        enter(PDNegotiationState::AttachSettle);
    }
}

// Monitor the active contract for rail loss or source-profile changes.
void PDManager::onReady(uint8_t status) {
    const bool railBelowTolerance = _inputValid && _voltage > 0.0f &&
        _inputVoltageV < _voltage * (1.0f - kVoltageToleranceFraction);
    if (railBelowTolerance) {
        if (_contractLostSinceMs == 0) {
            _contractLostSinceMs = millis();
        }
        if (millis() - _contractLostSinceMs >= kContractLostMs) {
            attemptRecovery();
            return;
        }
    } else {
        _contractLostSinceMs = 0;
    }

    if (status & NEWPDO_MSK) {
        DiagLog::event("new PDOs; renegotiating contract");
    } else {
        if (millis() - _stateEnteredAt < kProfileRecheckMs) {
            return;
        }
        _stateEnteredAt = millis();
        if (!_pd.refreshPdoTable()) {
            DiagLog::event("PDO refresh failed (ready)");
            return;
        }
        const uint8_t bestPdo = findBestFixedPdo();
        if (bestPdo == 0) {
            return;
        }

        const float bestVoltageV = _pd.getPdoVoltageV(bestPdo);
        const float bestCurrentA = _pd.getPdoMaxCurrentA(bestPdo);
        const bool voltageChanged = _voltage > 0.0f &&
            fabsf(bestVoltageV - _voltage) > _voltage * kVoltageToleranceFraction;
        const bool currentChanged = bestCurrentA != _current;
        if (!voltageChanged && !currentChanged) {
            return;
        }
        DiagLog::event("contract %.0fV/%.1fA->%.0fV/%.1fA",
                       _voltage, _current, bestVoltageV, bestCurrentA);
    }

    _voltage = 0.0f;
    _current = 0.0f;
    _contractChanged = true;
    enter(PDNegotiationState::AttachSettle);
    _lastPdoPollMs = 0;
}

// Re-request after trip/restart/loss.
bool PDManager::attemptRecovery() {
    if (_lastRecoveryMs != 0 && millis() - _lastRecoveryMs < kRecoveryBackoffMs) {
        return false;
    }
    _lastRecoveryMs = millis();
    _contractChanged = true;
    _contractLostSinceMs = 0;
    if (!_pd.refreshPdoTable()) {
        DiagLog::event("PDO refresh failed (recovery)");
        enter(PDNegotiationState::AttachSettle);
        _lastPdoPollMs = 0;
        return false;
    }
    if (!requestBestPdo()) {
        enter(PDNegotiationState::AttachSettle);
        _lastPdoPollMs = 0;
        return false;
    }
    enter(PDNegotiationState::Requesting);
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
        if (voltageV < kMinChargeVoltageV || voltageV > kMaxRequestVoltageV) {
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

bool PDManager::contractChanged() const {
    return _contractChanged;
}

float PDManager::getVoltage() const {
    return isReady() ? _voltage : 0.0f;
}

float PDManager::getCurrent() const {
    return isReady() ? _current : 0.0f;
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
