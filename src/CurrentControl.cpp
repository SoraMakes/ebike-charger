#include "CurrentControl.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed
static constexpr uint8_t kRegulatorEnablePin = D5;

static constexpr uint8_t kMaxStep = 127;

// Measured output-current calibration: 0.13 A at step 0, +0.0715 A per step.
static constexpr float kCurrentAtStepZeroA   = 0.13f;  // digipot: 125 Ohm
static constexpr float kCurrentPerStepA      = 0.0715f;  // digipot: 35,8 Ohm per step
static constexpr float kMaxAllowedOutputAmps = 3.0f;  // Software safety limit

void CurrentControl::begin(TwoWire &wire, VoltageMonitor &voltageMonitor) {
    _wire = &wire;
    _voltageMonitor = &voltageMonitor;
    pinMode(kRegulatorEnablePin, OUTPUT);
    digitalWrite(kRegulatorEnablePin, LOW);
    safe();
}

// TODO: remove again!
void CurrentControl::test(uint8_t maxLevel) {
    const uint8_t testMaxLevel = min(maxLevel, kMaxStep);

    safe();
    Serial.println("[CURRENT TEST] level -1, digipot step 0");
    Serial.println("[CURRENT TEST] starting next level in 20 seconds");
    delay(20000);

    for (uint8_t level = 0; level <= testMaxLevel; ++level) {
        _powerLevel = level;
        writeDigipot(level);
        digitalWrite(kRegulatorEnablePin, HIGH);
        Serial.printf("[CURRENT TEST] level %u, digipot step %u\n",
                      level, level);

        if (level < testMaxLevel) {
            delay(10000);
        }
    }
}

void CurrentControl::update(float limitWatts) {
    const int16_t previousLevel = _powerLevel;
    _voltageMonitor->update();
    const float outputVoltageV = _voltageMonitor->getVoltage();

    Serial.printf("[OUTPUT VOLTAGE] raw=%d voltage=%.2fV\n",
                  _voltageMonitor->getRawReading(),
                  outputVoltageV);


    if (limitWatts <= 0.0f || !isfinite(outputVoltageV) || outputVoltageV <= 0.0f) {
        safe();
        if (_powerLevel != previousLevel) {
            Serial.println("[CURRENT CONTROL] level -1, digipot step 0");
        }
        return;
    }

    const float targetAmps = limitWatts / outputVoltageV;
    const int16_t targetLevel = ampsToStep(min(targetAmps, kMaxAllowedOutputAmps));

    if (targetLevel < 0) {
        safe();
        if (_powerLevel != previousLevel) {
            Serial.println("[CURRENT CONTROL] level -1, digipot step 0");
        }
        return;
    }

    if (_powerLevel > targetLevel) {
        _powerLevel = targetLevel;
    } else if (_powerLevel < targetLevel) {
        ++_powerLevel;
    }

    const uint8_t step = static_cast<uint8_t>(_powerLevel);
    writeDigipot(step);
    digitalWrite(kRegulatorEnablePin, HIGH);

    if (_powerLevel != previousLevel) {
        Serial.printf("[CURRENT CONTROL] level %d, digipot step %u\n",
                      _powerLevel, step);
    }
}

void CurrentControl::safe() {
    _powerLevel = -1;
    digitalWrite(kRegulatorEnablePin, LOW);
    writeDigipot(0);
}

void CurrentControl::writeDigipot(uint8_t step) const {
    _wire->beginTransmission(kMCP4018Address);
    _wire->write(step);
    _wire->endTransmission();
}

int16_t CurrentControl::ampsToStep(float amps) {
    const float clampedAmps = constrain(amps, 0.0f, kMaxAllowedOutputAmps);

    if (clampedAmps < kCurrentAtStepZeroA) {
        return -1;
    }

    const float step = (clampedAmps - kCurrentAtStepZeroA) / kCurrentPerStepA;
    return static_cast<int16_t>(constrain(
        static_cast<int>(floorf(step)), 0, static_cast<int>(kMaxStep)
    ));
}
