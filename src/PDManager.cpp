#include "PDManager.h"

#include <math.h>

// Delay after attach detection before the controller's profile list is trusted.
static constexpr uint32_t kAttachSettleMs       = 300;
// How long to wait for a requested contract to become active before retrying.
static constexpr uint32_t kNegotiationTimeoutMs = 2000;
// While Ready, how often to re-check the source's profile list for changes
// (in addition to reacting immediately to the NEWPDO status bit).
static constexpr uint32_t kProfileRecheckMs     = 500;

bool PDManager::begin(TwoWire &wire, int8_t intPin) {
    _intPin = intPin;
    if (_intPin >= 0) {
        pinMode(_intPin, INPUT);
    }

    if (!_pd.begin(wire)) {
        return false;
    }

    _pd.clearConfig(AP33772_CONFIG::UVP_EN); // required for RotoPD Pro; avoids UVP hard resets
    _pd.setInterruptRegisterMask(STARTED_MSK | READY_MSK | NEWPDO_MSK);
    _pd.setOutput(0); // VBUS off until a contract is confirmed
    return true;
}

bool PDManager::isSourcePhysicallyPresent() {
    const uint8_t opMode = _pd.readOpMode();
    // Bit 1 = PDMOD (PD source), Bit 0 = LGCYMOD (Legacy non-PD source)
    if ((opMode & 0x03) != 0) {
        return true;
    }
    // Fallback check: VBUS voltage >= 3.0V
    return (_pd.readVoltage() >= 3000);
}

void PDManager::update() {
    _justAttached  = false;
    _justDetached  = false;
    _voltageJumped = false;

    if (_shutdownLatched) {
        return;
    }

    uint8_t status = 0;
    if (_intPin >= 0 && digitalRead(_intPin) == HIGH) {
        status = _pd.readStatus();
    }

    // Handle physical disconnection
    if (_state != PDNegotiationState::Disconnected) {
        if (!isSourcePhysicallyPresent()) {
            if (_state == PDNegotiationState::Ready) {
                _justDetached = true;
            }
            _pd.setOutput(0);
            _state          = PDNegotiationState::Disconnected;
            _activePdoIndex = 0;
            _voltage        = 0.0f;
            _current        = 0.0f;
            return;
        }
    }

    switch (_state) {
        case PDNegotiationState::Disconnected:
            if ((status & STARTED_MSK) || isSourcePhysicallyPresent()) {
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;

        case PDNegotiationState::AttachSettle:
            if ((status & NEWPDO_MSK) || (millis() - _stateEnteredAt >= kAttachSettleMs)) {
                _pd.refreshPdoTable();
                if (requestBestPdo()) {
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                }
                // else: no eligible fixed PDO yet; keep retrying from AttachSettle.
            }
            break;

        case PDNegotiationState::Requesting: {
            const bool msgSuccess     = (_pd.readMsgResult() == 1);
            const float activeVoltage = _pd.readVREQ() / 1000.0f;
            const bool voltageMatched = (_voltage > 0.0f) && (fabsf(activeVoltage - _voltage) <= 0.5f);

            if ((status & READY_MSK) || msgSuccess || voltageMatched) {
                _state = PDNegotiationState::Ready;
                _pd.setOutput(1);
                _justAttached = true;
                Serial.printf("[PD] Contract active: %.1fV %.2fA %.1fW\n",
                              _voltage, _current, _voltage * _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                Serial.println("[PD] Contract request timed out; retrying...");
                _state          = PDNegotiationState::AttachSettle;
                _stateEnteredAt = millis();
            }
            break;
        }

        case PDNegotiationState::Ready: {
            const bool dueForRecheck = (status & NEWPDO_MSK) ||
                (millis() - _stateEnteredAt >= kProfileRecheckMs);
            if (!dueForRecheck) {
                break;
            }
            _stateEnteredAt = millis();
            _pd.refreshPdoTable();

            const uint8_t bestPdo = findBestFixedPdo();
            if (bestPdo != 0 && bestPdo != _activePdoIndex) {
                Serial.printf("[PD] Source profile changed (%.1fV -> %.1fV); re-negotiating.\n",
                              _voltage, _pd.getPdoVoltageV(bestPdo));
                _voltageJumped = true;
                _pd.setOutput(0);
                if (requestBestPdo()) {
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                }
            }
            break;
        }
    }
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

void PDManager::shutdown() {
    if (_shutdownLatched) {
        return;
    }

    _shutdownLatched = true;
    Serial.println("[PD] Shutdown: switching VBUS off");
    _pd.setOutput(0);
}
