#include "PDManager.h"

static constexpr uint32_t kAttachTimeoutMs = 5000;
static constexpr uint32_t kPDHandshakeTimeoutMs = 3000;

static constexpr float kVoltages[] = { 0.0f, 5.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f };
static constexpr float kCurrents[] = {
    0.5f, 0.7f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.50f, 2.75f, 3.0f, 3.25f, 3.5f, 4.0f, 4.5f, 5.0f
};

bool PDManager::begin(TwoWire &wire) {
    return _husb.begin(HUSB238_I2CADDR_DEFAULT, &wire);
}

PDMode PDManager::detectMode() {
    if (!_waitForAttach(kAttachTimeoutMs)) {
        return PDMode::NO_CHARGER;
    }

    if (!_waitForPDHandshake(kPDHandshakeTimeoutMs)) {
        return PDMode::FIVE_VOLT_ONLY;
    }

    const float activeV = getNegotiatedVoltage();
    const float activeA = getNegotiatedCurrentAmps();

    Serial.printf("[PDManager] Active: %.0fV @ %.2fA (%.1fW)\n", activeV, activeA, activeV * activeA);

    if (activeV > 5.0f) {
        return PDMode::HIGH_VOLTAGE;
    }

    logAvailablePDOs();

    float bestWatts = 0.0f;
    HUSB238_PDSelection bestPDO = _findBestHighVoltagePDO(bestWatts);

    if (bestPDO == PD_NOT_SELECTED) {
        return PDMode::FIVE_VOLT_ONLY;
    }

    const float targetV = _voltageFromSelection(bestPDO);
    Serial.printf("[PDManager] Requesting %.0fV (%.1fW)\n", targetV, bestWatts);
    Serial.flush();

    _requestPDO(bestPDO);

    // Give negotiation time to apply. If voltage change causes a brownout, 
    // the system naturally reboots. If not, trigger a manual restart.
    delay(2000);
    Serial.println("[PDManager] Restarting to apply...");
    delay(100);
    ESP.restart();

    return PDMode::NEGOTIATING;
}

float PDManager::getNegotiatedVoltage() {
    return _voltageFromActiveSetting(_husb.getPDSrcVoltage());
}

float PDManager::getNegotiatedCurrentAmps() {
    return _ampsFromCurrentSetting(_husb.getPDSrcCurrent());
}

void PDManager::logAvailablePDOs() {
    static const HUSB238_PDSelection kList[] = {
        PD_SRC_5V, PD_SRC_9V, PD_SRC_12V, PD_SRC_15V, PD_SRC_18V, PD_SRC_20V
    };
    Serial.println("[PDManager] Advertised source capabilities:");
    for (HUSB238_PDSelection pdo : kList) {
        if (!_husb.isVoltageDetected(pdo)) continue;
        const float v = _voltageFromSelection(pdo);
        const float a = _ampsFromCurrentSetting(_husb.currentDetected(pdo));
        Serial.printf("  - %.0fV @ %.2fA (%.1fW)\n", v, a, v * a);
    }
}

bool PDManager::_waitForAttach(uint32_t timeoutMs) {
    Serial.print("[PDManager] Waiting for charger");
    const uint32_t deadline = millis() + timeoutMs;
    while (!_husb.isAttached()) {
        if (millis() > deadline) {
            Serial.println(" [TIMEOUT]");
            return false;
        }
        Serial.print(".");
        delay(200);
    }
    Serial.println(" OK");
    return true;
}

bool PDManager::_waitForPDHandshake(uint32_t timeoutMs) {
    Serial.print("[PDManager] Waiting for PD contract");
    const uint32_t deadline = millis() + timeoutMs;
    while (_husb.getPDResponse() != SUCCESS) {
        if (millis() > deadline) {
            Serial.println(" [TIMEOUT]");
            return false;
        }
        Serial.print(".");
        delay(100);
    }
    Serial.println(" OK");
    return true;
}

HUSB238_PDSelection PDManager::_findBestHighVoltagePDO(float &outWatts) {
    static const HUSB238_PDSelection kList[] = {
        PD_SRC_9V, PD_SRC_12V, PD_SRC_15V, PD_SRC_18V, PD_SRC_20V
    };
    HUSB238_PDSelection best = PD_NOT_SELECTED;
    outWatts = 0.0f;
    for (HUSB238_PDSelection pdo : kList) {
        if (!_husb.isVoltageDetected(pdo)) continue;
        const float v = _voltageFromSelection(pdo);
        const float a = _ampsFromCurrentSetting(_husb.currentDetected(pdo));
        const float w = v * a;
        if (w > outWatts) {
            outWatts = w;
            best = pdo;
        }
    }
    return best;
}

void PDManager::_requestPDO(HUSB238_PDSelection pdo) {
    _husb.selectPD(pdo);
    _husb.requestPD();
}

float PDManager::_voltageFromSelection(HUSB238_PDSelection sel) {
    switch (sel) {
        case PD_SRC_5V:  return 5.0f;
        case PD_SRC_9V:  return 9.0f;
        case PD_SRC_12V: return 12.0f;
        case PD_SRC_15V: return 15.0f;
        case PD_SRC_18V: return 18.0f;
        case PD_SRC_20V: return 20.0f;
        default:         return 0.0f;
    }
}

float PDManager::_voltageFromActiveSetting(HUSB238_VoltageSetting vs) {
    size_t idx = static_cast<size_t>(vs);
    return (idx < (sizeof(kVoltages) / sizeof(kVoltages[0]))) ? kVoltages[idx] : 0.0f;
}

float PDManager::_ampsFromCurrentSetting(HUSB238_CurrentSetting cur) {
    size_t idx = static_cast<size_t>(cur);
    return (idx < (sizeof(kCurrents) / sizeof(kCurrents[0]))) ? kCurrents[idx] : 0.0f;
}
