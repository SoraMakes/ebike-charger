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

void PDManager::applyStartupConfig() {
    if (_configured) {
        return;
    }
    // False UVP on RotoPD Pro.
    _pd.clearConfig(AP33772_CONFIG::UVP_EN);
    if (!_pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK | kFaultMask)) {
        if (!_configFailureLogged) {
            DiagLog::event("PD mask write failed");
            _configFailureLogged = true;
        }
        return;
    }
    _configFailureLogged = false;
    _configured = true;
}

void PDManager::setMeasuredInputVoltage(float volts) {
    _inputVoltageV = volts;
}

uint8_t PDManager::pollStatus() {
    const uint8_t status = _pd.readStatus();
    if (status == 0) {
        return 0;
    }
    const uint8_t freshFaults = static_cast<uint8_t>(status & kFaultMask & ~_statusSticky);
    _statusSticky |= status;
    if (freshFaults != 0) {
        DiagLog::event("chip fault %s", getProtectionFaultText());
    }
    return status;
}

const char *PDManager::getProtectionFaultText() const {
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

bool PDManager::hasProtectionFault() const {
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
    if (_state != PDNegotiationState::Disconnected && !_configured) {
        applyStartupConfig();
    }
    handleChipRestart(status);
    handleProtectionTrip(status);

    switch (_state) {
        case PDNegotiationState::Disconnected: onDisconnected(status, opMode); break;
        case PDNegotiationState::WaitingForSourceCapabilities: onWaitingForSourceCapabilities(status); break;
        case PDNegotiationState::Requesting: onRequesting(); break;
        case PDNegotiationState::Ready: onReady(status); break;
    }
}

void PDManager::enter(PDNegotiationState state) {
    _state = state;
    _stateEnteredAt = millis();
}

// STATUS is read-clear and polled on INT/fallback; OPMODE is sampled each update for source detection.
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
        // DiagLog::event("opmode 0x%02X", opMode);
        _lastOpMode = opMode;
    }
    if (status != _lastStatus) {
        DiagLog::event("status 0x%02X", status);
        _lastStatus = status;
    }
    return status;
}

void PDManager::handleChipRestart(uint8_t status) {
    if (!(status & STARTED_MSK) || _state == PDNegotiationState::Disconnected) {
        return;
    }
    // Chip restart: re-apply config.
    _configured = false;
    applyStartupConfig();
    if (_state == PDNegotiationState::Ready) {
        attemptRecovery();
    } else if (_state == PDNegotiationState::Requesting) {
        beginRenegotiation();
        _nextRequestAllowedMs = 0;
    } else {
        // Preserve VOUT while waiting for source capabilities (capacitor precharge).
        _nextRequestAllowedMs = 0;
        _lastPdoPollMs = 0;
    }
}

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
    // Connect at the default 5 V profile to precharge downstream input capacitors.
    _pd.setOutput(1);
    enter(PDNegotiationState::WaitingForSourceCapabilities);
    _lastPdoPollMs = 0;
    _nextRequestAllowedMs = 0;
}

void PDManager::onWaitingForSourceCapabilities(uint8_t status) {
    const uint32_t now = millis();
    // Allow polling when no backoff is set or its deadline has passed.
    const bool requestAllowed = _nextRequestAllowedMs == 0 || static_cast<int32_t>(now - _nextRequestAllowedMs) >= 0;
    // Poll on new PDOs, the first pass, or when the polling interval elapses.
    const bool pollDue = (status & NEWPDO_MSK) || (_lastPdoPollMs == 0) ||
        (now - _lastPdoPollMs >= kSourceCapabilitiesPollMs);

    if (requestAllowed && pollDue) {
        _lastPdoPollMs = now;
        if (!_pd.refreshPdoTable()) {
            DiagLog::event("PDO refresh failed (attach)");
            return;
        }
        if (requestBestPdo()) {
            enter(PDNegotiationState::Requesting);
        }
    }
}

void PDManager::onRequesting() {
    // The controller reports 0 while pending, 1 on success, and 2 or higher on rejection.
    const uint8_t response = static_cast<uint8_t>(_pd.readMsgResult() & 0x07);
    if (response >= 2) {
        onRequestRejected();
        return;
    }

    if (response == 1 && contractMatchesRequest()) {
        onRequestAccepted();
        return;
    }

    if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
        enter(PDNegotiationState::WaitingForSourceCapabilities);
    }
}

bool PDManager::contractMatchesRequest() {
    const float activeVoltageV = _pd.readVREQ() / 1000.0f;
    const float activeCurrentA = _pd.readIREQ() / 1000.0f;
    return activeVoltageV == _voltage && activeCurrentA == _current;
}

void PDManager::onRequestRejected() {
    _voltage = 0.0f;
    _current = 0.0f;
    _nextRequestAllowedMs = millis() + kRecoveryBackoffMs;
    enter(PDNegotiationState::WaitingForSourceCapabilities);
}

void PDManager::onRequestAccepted() {
    enter(PDNegotiationState::Ready);
    _justAttached = true;
    _contractLostSinceMs = 0;
    _pd.setOutput(1);
    DiagLog::event("contract %.0fV %.1fA", _voltage, _current);
}

void PDManager::onReady(uint8_t status) {
    if (contractLossConfirmed()) {
        attemptRecovery();
        return;
    }

    if (shouldRenegotiate(status)) {
        beginRenegotiation();
    }
}

bool PDManager::contractLossConfirmed() {
    const uint32_t now = millis();
    // True when a valid measured rail is below the active contract tolerance.
    const bool railBelowTolerance = isfinite(_inputVoltageV) && _voltage > 0.0f &&
        _inputVoltageV < _voltage * (1.0f - kVoltageToleranceFraction);
    if (!railBelowTolerance) {
        _contractLostSinceMs = 0;
        return false;
    }

    if (_contractLostSinceMs == 0) {
        _contractLostSinceMs = now;
    }
    return now - _contractLostSinceMs >= kContractLostMs;
}

bool PDManager::shouldRenegotiate(uint8_t status) {
    if (status & NEWPDO_MSK) {
        DiagLog::event("new PDOs; renegotiating contract");
        return true;
    }

    if (!refreshPdoTableIfDue()) {
        return false;
    }
    return detectBestPdoContractChange();
}

bool PDManager::refreshPdoTableIfDue() {
    const uint32_t now = millis();
    if (now - _stateEnteredAt < kProfileRecheckMs) {
        return false;
    }
    _stateEnteredAt = now;
    if (!_pd.refreshPdoTable()) {
        DiagLog::event("PDO refresh failed (ready)");
        return false;
    }
    return true;
}

bool PDManager::detectBestPdoContractChange() {
    const uint8_t bestPdo = findBestFixedPdo();
    // No fixed PDO meets the voltage and current requirements.
    if (bestPdo == 0) {
        return false;
    }
    const float bestVoltageV = _pd.getPdoVoltageV(bestPdo);
    const float bestCurrentA = _pd.getPdoMaxCurrentA(bestPdo);
    // A selected PDO voltage change requires a new contract.
    const bool voltageChanged = bestVoltageV != _voltage;
    const bool currentChanged = bestCurrentA != _current;
    if (!voltageChanged && !currentChanged) {
        return false;
    }
    DiagLog::event("contract %.0fV/%.1fA->%.0fV/%.1fA", _voltage, _current, bestVoltageV, bestCurrentA);
    return true;
}

void PDManager::beginRenegotiation() {
    // Keep VOUT connected as the source transitions to the requested profile.
    _voltage = 0.0f;
    _current = 0.0f;
    _contractChanged = true;
    enter(PDNegotiationState::WaitingForSourceCapabilities);
    // Make the next source-capability poll run immediately.
    _lastPdoPollMs = 0;
}

bool PDManager::attemptRecovery() {
    beginRenegotiation();
    _contractLostSinceMs = 0;
    const uint32_t now = millis();
    // True while a previous recovery attempt is still within its retry backoff.
    const bool recoveryBackoffActive = _lastRecoveryMs != 0 && now - _lastRecoveryMs < kRecoveryBackoffMs;
    if (recoveryBackoffActive) {
        _nextRequestAllowedMs = _lastRecoveryMs + kRecoveryBackoffMs;
        return false;
    }
    _lastRecoveryMs = now;
    if (!_pd.refreshPdoTable()) {
        DiagLog::event("PDO refresh failed (recovery)");
        return false;
    }
    if (!requestBestPdo()) {
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

    if (!_pd.requestFixedPDO(bestPdo)) {
        return false;
    }

    _voltage = _pd.getPdoVoltageV(bestPdo);
    _current = _pd.getPdoMaxCurrentA(bestPdo);
    return true;
}

uint8_t PDManager::findBestFixedPdo() const {
    uint8_t bestPdoIndex = 0;
    float bestVoltageV = 0.0f;
    for (int pdoIndex = 1; pdoIndex <= MAX_PDO_ENTRIES; ++pdoIndex) {
        const float pdoVoltageV = _pd.getPdoVoltageV(pdoIndex);
        if (pdoVoltageV < kMinChargeVoltageV || pdoVoltageV > kMaxRequestVoltageV) {
            continue;
        }
        // Ignore PDOs whose advertised current does not guarantee a positive minimum.
        const float maxCurrentA = _pd.getPdoMaxCurrentA(pdoIndex);
        if (maxCurrentA <= 0.0f) {
            continue;
        }
        if (pdoVoltageV > bestVoltageV) {
            bestVoltageV = pdoVoltageV;
            bestPdoIndex = static_cast<uint8_t>(pdoIndex);
        }
    }
    return bestPdoIndex;
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
