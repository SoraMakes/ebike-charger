#include "CurrentControl.h"
#include "PDManager.h"
#include "DiagLog.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;
static constexpr uint32_t kIncreaseIntervalMs = 250;
// These final steps will be increases one by one and not accelerated
static constexpr int16_t kFinalRampSteps = 4;
// Four increase intervals per second; divisor 8 gives 50% of target per second.
static constexpr int16_t kRampUpDivisor = 8;
static constexpr uint32_t kVoltageLogIntervalMs = 1000;

// Measured output-current calibration (actual current vs digipot step).
// Recalibrated on the final ESP32 assembly from three measured points:
//   set 0.99 A -> 0.800 A, 2.20 A -> 1.775 A, 0.63 A -> 0.522 A
// Least-squares fit: actual = 0.80004 * configured + 0.01362, which maps to
//   actual = 0.1176 A at step 0, +0.0572 A per step.
static constexpr float kCurrentAtStepZeroA   = 0.14f;  // digipot: 125 Ohm
static constexpr float kCurrentPerStepA      = 0.0572f;  // digipot: 35,8 Ohm per step
static constexpr float kMaxAllowedOutputAmps = 3.0f;  // Software safety limit

void CurrentControl::begin(TwoWire &wire, VoltageMonitor &voltageMonitor, PDManager &pdManager) {
    _wire = &wire;
    _voltageMonitor = &voltageMonitor;
    _pdManager = &pdManager;
    _lastDigipotStep = -1;

    // Read the wiper BEFORE overwriting it. The MCP4018's wiper is volatile and
    // is documented to power up at mid-scale, so after a brownout the regulator
    // has been commanded at roughly half of kMaxAllowedOutputAmps for the whole
    // boot. Logging the value we find turns that from a datasheet claim into a
    // measurement on this board: a value near 64 on a reboot confirms it.
    _wire->requestFrom(static_cast<uint8_t>(kMCP4018Address), static_cast<uint8_t>(1));
    if (_wire->available()) {
        const uint8_t wiperAtBoot = static_cast<uint8_t>(_wire->read());
        DiagLog::event("digipot at boot: step %u (~%.2fA)", wiperAtBoot,
                       kCurrentAtStepZeroA + wiperAtBoot * kCurrentPerStepA);
    } else {
        DiagLog::event("digipot at boot: no response from 0x%02X", kMCP4018Address);
    }

    safe();
}

// TODO: remove again!
void CurrentControl::test(uint8_t maxLevel) {
    if (_shutdownLatched) {
        safe();
        return;
    }
    const uint8_t testMaxLevel = min(maxLevel, kMaxStep);

    safe();
    Serial.println("[CURRENT TEST] level -1, digipot step 0");
    Serial.println("[CURRENT TEST] starting next level in 20 seconds");
    delay(20000);

    for (uint8_t level = 0; level <= testMaxLevel; ++level) {
        _powerLevel = level;
        writeDigipot(level);

        Serial.printf("[CURRENT TEST] level %u, digipot step %u\n",
                      level, level);

        if (level < testMaxLevel) {
            delay(10000);
        }
    }
}

// TODO: remove
void CurrentControl::test_set_fixed(uint8_t level) {
    if (_shutdownLatched) {
        safe();
        return;
    }
    const uint8_t testLevel = min(level, kMaxStep);

    _powerLevel = testLevel;
    writeDigipot(testLevel);
    Serial.printf("[CURRENT TEST] level %u, digipot step %u\n",
                  testLevel, testLevel);
    delay(5*60*1000);  // 5 minutes
}

void CurrentControl::update(float limitWatts) {
    if (_shutdownLatched) {
        safe();
        return;
    }

    const int16_t previousLevel = _powerLevel;
    const uint32_t now = millis();
    const float outputVoltageV = _voltageMonitor->getVoltage();

    if (limitWatts <= 0.0f || !isfinite(outputVoltageV) || outputVoltageV <= 0.0f) {
        safe();
        if (_powerLevel != previousLevel) {
            Serial.println("[CURRENT CONTROL] level -1, digipot step 0");
        }
        return;
    }

    const float targetAmps = limitWatts / outputVoltageV;
    const float currentLimitA = min(targetAmps, kMaxAllowedOutputAmps);
    const int16_t targetLevel = ampsToStep(currentLimitA);
    _targetLevel = targetLevel;

    if (targetLevel < 0) {
        safe();
        if (_powerLevel != previousLevel) {
            Serial.println("[CURRENT CONTROL] level -1, digipot step 0");
        }
        return;
    }

    const int16_t acceleratedTarget = max(
        static_cast<int16_t>(0),
        static_cast<int16_t>(targetLevel - kFinalRampSteps)
    );

    if (_powerLevel > targetLevel) {
        _powerLevel = targetLevel;
    } else if (_powerLevel < targetLevel) {
        if (now - _lastIncreaseAt >= kIncreaseIntervalMs) {
            _lastIncreaseAt = now;

            if (_powerLevel < acceleratedTarget) {
                const int16_t stepsPerIncrease = max(
                    static_cast<int16_t>(1),
                    static_cast<int16_t>(targetLevel / kRampUpDivisor)
                );
                _powerLevel = min(
                    static_cast<int16_t>(_powerLevel + stepsPerIncrease),
                    acceleratedTarget
                );
            } else {
                // Final steps: one step per timer interval.
                ++_powerLevel;
            }
        }
    }

    // -1 means disabled internally; step 0 is the safe digipot value.
    const uint8_t step = _powerLevel < 0
        ? 0
        : static_cast<uint8_t>(_powerLevel);
    writeDigipot(step);

    if (_powerLevel != previousLevel) {
        const float configuredCurrentA = kCurrentAtStepZeroA + static_cast<float>(_powerLevel) * kCurrentPerStepA;
        Serial.printf("[CURRENT CONTROL] level: %d, digipot step: %u, output voltage: %.2fV, limit: %.2fA %.2fW\n",
                      _powerLevel, step, outputVoltageV, configuredCurrentA, outputVoltageV * configuredCurrentA);
    }
}

void CurrentControl::safe() {
    _powerLevel = -1;
    _targetLevel = -1;
    // Restart the ramp clock. Without this, _lastIncreaseAt keeps ageing while
    // the regulator is held safe, so the first update() after the gate reopens
    // finds the interval already elapsed and jumps a full accelerated step
    // immediately instead of ramping from zero.
    _lastIncreaseAt = millis();
    writeDigipot(0);
}

float CurrentControl::getCurrentLimitA() const {
    if (_powerLevel < 0) {
        return 0.0f;
    }
    return kCurrentAtStepZeroA + static_cast<float>(_powerLevel) * kCurrentPerStepA;
}

bool CurrentControl::isRampUpFinished() const {
    return _powerLevel >= 0 && _powerLevel >= _targetLevel;
}

void CurrentControl::requestShutdown(const char *reason) {
    if (_shutdownLatched) {
        return;
    }

    _shutdownLatched = true;
    strncpy(_shutdownReason, reason ? reason : "unknown", sizeof(_shutdownReason) - 1);
    _shutdownReason[sizeof(_shutdownReason) - 1] = '\0';
    Serial.printf("[CURRENT CONTROL] Shutdown: %s\n", _shutdownReason);
    _pdManager->shutdown();
    safe();
}

bool CurrentControl::isShutdownLatched() const {
    return _shutdownLatched;
}

const char *CurrentControl::getShutdownReason() const {
    return _shutdownReason;
}

void CurrentControl::writeDigipot(uint8_t step) {
    if (_lastDigipotStep == step) {
        return;
    }

//    Serial.printf("[DIGIPOT] writing step %u\n", step);
    _wire->beginTransmission(kMCP4018Address);
    _wire->write(step);
    if (_wire->endTransmission() == 0) {
        _lastDigipotStep = step;
    }
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
