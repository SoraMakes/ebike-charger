#include "PDManager.h"

// Delay after attach detection before the source is asked for its
// capabilities, giving CC/cable detection time to settle.
static constexpr uint32_t kAttachSettleMs        = 300;
// How long to wait for a capability/contract response before retrying.
static constexpr uint32_t kNegotiationTimeoutMs  = 2000;
// Ignore a few failed attachment reads; an I2C failure is not a disconnect.
static constexpr uint8_t kDetachReadThreshold    = 3;

bool PDManager::begin(TwoWire &wire) {
    return _husb.begin(HUSB238_I2CADDR_DEFAULT, &wire);
}

void PDManager::update() {
    _justAttached  = false;
    _justDetached  = false;
    _voltageJumped = false;

    const bool attached = _husb.isAttached();

    if (!attached) {
        if (_state == PDNegotiationState::Disconnected) {
            return;
        }

        if (_failedAttachReads < kDetachReadThreshold) {
            ++_failedAttachReads;
        }
        if (_failedAttachReads < kDetachReadThreshold) {
            return;
        }

        if (_state == PDNegotiationState::Ready) {
            _justDetached = true;
        }
        _state   = PDNegotiationState::Disconnected;
        _voltage = 0.0f;
        _current = 0.0f;
        return;
    }

    _failedAttachReads = 0;

    switch (_state) {
        case PDNegotiationState::Disconnected:
            _state          = PDNegotiationState::AttachSettle;
            _stateEnteredAt = millis();
            break;

        case PDNegotiationState::AttachSettle:
            if (millis() - _stateEnteredAt >= kAttachSettleMs) {
                requestCapabilities();
            }
            break;

        case PDNegotiationState::WaitingCapabilities:
            if (_husb.getPDResponse() == SUCCESS) {
                if (selectAndRequestBestPdo()) {
                    _state          = PDNegotiationState::Requesting;
                    _stateEnteredAt = millis();
                } else {
                    // No PDO above 5 V available; the default 5 V contract is
                    // already active.
                    _voltage      = 5.0f;
                    _current      = _ampsFromCurrentSetting(_husb.getPDSrcCurrent());
                    _state        = PDNegotiationState::Ready;
                    _justAttached = true;
                }
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                Serial.println("[PD] Capabilities not ready; retrying...");
                requestCapabilities();
            }
            break;

        case PDNegotiationState::Requesting: {
            const float activeVoltage = _voltageFromActiveSetting(_husb.getPDSrcVoltage());
            if (activeVoltage <= 0.0f) {
                // An invalid enum usually means a transient I2C read failure.
                // Keep waiting instead of treating it as a real contract change.
                break;
            }

            if (activeVoltage == _requestedVoltage) {
                _voltage      = activeVoltage;
                _current      = _ampsFromCurrentSetting(_husb.getPDSrcCurrent());
                _state        = PDNegotiationState::Ready;
                _justAttached = true;
                Serial.printf("[PD] Contract active: %.0fV %.2fA %.1fW\n",
                              _voltage, _current, _voltage * _current);
            } else if (millis() - _stateEnteredAt >= kNegotiationTimeoutMs) {
                Serial.println("[PD] Contract request timed out; retrying...");
                requestCapabilities();
            }
            break;
        }

        case PDNegotiationState::Ready: {
            const float activeVoltage = _voltageFromActiveSetting(_husb.getPDSrcVoltage());
            if (activeVoltage <= 0.0f) {
                // Do not renegotiate on one invalid HUSB238 register read.
                break;
            }

            if (activeVoltage != _voltage) {
                Serial.printf("[PD] Contract changed: %.0fV -> %.0fV; re-negotiating.\n",
                              _voltage, activeVoltage);
                _voltageJumped = true;
                requestCapabilities();
            }
            break;
        }
    }
}

void PDManager::requestCapabilities() {
    _state          = PDNegotiationState::WaitingCapabilities;
    _stateEnteredAt = millis();
}

bool PDManager::selectAndRequestBestPdo() {
    static constexpr struct { HUSB238_PDSelection sel; float volts; } kPDOs[] = {
        {PD_SRC_20V, 20.f}, {PD_SRC_18V, 18.f}, {PD_SRC_15V, 15.f},
        {PD_SRC_12V, 12.f}, {PD_SRC_9V, 9.f},   {PD_SRC_5V, 5.f},
    };

    Serial.println("[PD] Available source profiles:");

    for (const auto &pdo : kPDOs) {
        const bool available = _husb.isVoltageDetected(pdo.sel);
        if (!available) {
            continue;
        }

        const float amps = _ampsFromCurrentSetting(_husb.currentDetected(pdo.sel));
        Serial.printf("[PD] %.0fV %.2fA %.1fW\n", pdo.volts, amps, pdo.volts * amps);

        if (pdo.volts <= 5.0f) {
            // Already the default contract; nothing to request.
            return false;
        }

        _husb.selectPD(pdo.sel);
        _husb.requestPD();
        _requestedVoltage = pdo.volts;
        Serial.printf("[PD] Requested %.0fV, response=%u, selected=%u\n",
                  _requestedVoltage,
                  static_cast<unsigned>(_husb.getPDResponse()),
                  static_cast<unsigned>(_husb.getSelectedPD()));
        return true;
    }

    return false;
}

bool PDManager::isAttached() const {
    return _state != PDNegotiationState::Disconnected;
}

bool PDManager::isReady() const {
    return _state == PDNegotiationState::Ready;
}

bool PDManager::justAttached() {
    return _justAttached;
}

bool PDManager::justDetached() {
    return _justDetached;
}

bool PDManager::voltageJumped() {
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

float PDManager::_voltageFromActiveSetting(HUSB238_VoltageSetting vs) {
    switch (vs) {
        case PD_5V:  return 5.0f;
        case PD_9V:  return 9.0f;
        case PD_12V: return 12.0f;
        case PD_15V: return 15.0f;
        case PD_18V: return 18.0f;
        case PD_20V: return 20.0f;
        default:     return 0.0f;
    }
}

float PDManager::_ampsFromCurrentSetting(HUSB238_CurrentSetting cur) {
    switch (cur) {
        case CURRENT_0_5_A:  return 0.5f;
        case CURRENT_0_7_A:  return 0.7f;
        case CURRENT_1_0_A:  return 1.0f;
        case CURRENT_1_25_A: return 1.25f;
        case CURRENT_1_5_A:  return 1.5f;
        case CURRENT_1_75_A: return 1.75f;
        case CURRENT_2_0_A:  return 2.0f;
        case CURRENT_2_25_A: return 2.25f;
        case CURRENT_2_50_A: return 2.5f;
        case CURRENT_2_75_A: return 2.75f;
        case CURRENT_3_0_A:  return 3.0f;
        case CURRENT_3_25_A: return 3.25f;
        case CURRENT_3_5_A:  return 3.5f;
        case CURRENT_4_0_A:  return 4.0f;
        case CURRENT_4_5_A:  return 4.5f;
        case CURRENT_5_0_A:  return 5.0f;
        default:             return 0.0f;
    }
}
