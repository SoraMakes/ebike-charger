#include "CurrentControl.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;
static constexpr uint32_t kIncreaseIntervalMs = 250;
static constexpr int16_t kFinalRampSteps = 4;
static constexpr int16_t kRampUpDivisor = 8;

// Output-current calibration.
static constexpr float kCurrentAtStepZeroA   = 0.14f;
static constexpr float kCurrentPerStepA      = 0.0572f;
static constexpr float kMaxAllowedOutputAmps = 3.0f;

// No-load hold step (~0.37A).
static constexpr int16_t kNoLoadHoldStep = 4;

void CurrentControl::begin(TwoWire &wire, VoltageMonitor &voltageMonitor) {
    _wire = &wire;
    _voltageMonitor = &voltageMonitor;
    _lastDigipotStep = -1;
    minimumCurrent();
}

void CurrentControl::update(float limitWatts, bool noLoad) {
    const uint32_t now = millis();
    const float outputVoltageV = _voltageMonitor->getVoltage();

    if (limitWatts <= 0.0f || !isfinite(outputVoltageV) || outputVoltageV <= 0.0f) {
        minimumCurrent();
        return;
    }

    const float targetAmps = limitWatts / outputVoltageV;
    const float currentLimitA = min(targetAmps, kMaxAllowedOutputAmps);
    const int16_t targetLevel = ampsToStep(currentLimitA);
    _targetLevel = targetLevel;

    // No-load hold.
    if (noLoad && targetLevel > kNoLoadHoldStep) {
        _powerLevel = kNoLoadHoldStep;
        _lastStepChangeAt = now;
        writeDigipot(static_cast<uint8_t>(kNoLoadHoldStep));
        return;
    }

    const int16_t acceleratedTarget = max(
        static_cast<int16_t>(0),
        static_cast<int16_t>(targetLevel - kFinalRampSteps)
    );

    if (_powerLevel > targetLevel) {
        _powerLevel = targetLevel;
    } else if (_powerLevel < targetLevel) {
        if (now - _lastStepChangeAt >= kIncreaseIntervalMs) {
            _lastStepChangeAt = now;

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

    writeDigipot(static_cast<uint8_t>(_powerLevel));
}

void CurrentControl::minimumCurrent() {
    _powerLevel = 0;
    _targetLevel = 0;
    _lastStepChangeAt = millis();
    writeDigipot(0);
}

float CurrentControl::getCurrentLimitA() const {
    return kCurrentAtStepZeroA + static_cast<float>(_powerLevel) * kCurrentPerStepA;
}

bool CurrentControl::isRampUpFinished() const {
    return _powerLevel >= _targetLevel;
}

void CurrentControl::writeDigipot(uint8_t step) {
    if (_lastDigipotStep == step) {
        return;
    }

    _wire->beginTransmission(kMCP4018Address);
    _wire->write(step);
    if (_wire->endTransmission() == 0) {
        _lastDigipotStep = step;
    }
}

int16_t CurrentControl::ampsToStep(float amps) {
    const float clampedAmps = constrain(amps, 0.0f, kMaxAllowedOutputAmps);

    const float step = (clampedAmps - kCurrentAtStepZeroA) / kCurrentPerStepA;
    return static_cast<int16_t>(constrain(
        static_cast<int>(floorf(step)), 0, static_cast<int>(kMaxStep)
    ));
}
