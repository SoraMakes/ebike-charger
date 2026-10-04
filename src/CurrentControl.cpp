#include "CurrentControl.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;
static constexpr uint32_t kIncreaseIntervalMs = 250;
// Steps reserved for the slow, one-at-a-time approach to the target.
static constexpr int16_t kFinalRampSteps = 4;
// Current-limit digipot step divided by this value sets coarse ramp steps per interval.
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
    const int16_t currentLimitDigipotStep = ampsToStep(currentLimitA);
    _currentLimitDigipotStep = currentLimitDigipotStep;

    // During no-load periods, hold the output at a low setpoint until load rises.
    if (noLoad && currentLimitDigipotStep > kNoLoadHoldStep) {
        _outputDigipotStep = kNoLoadHoldStep;
        _lastStepChangeAt = now;
        writeDigipot(static_cast<uint8_t>(kNoLoadHoldStep));
        return;
    }

    const int16_t acceleratedTarget = max(
        static_cast<int16_t>(0),
        static_cast<int16_t>(currentLimitDigipotStep - kFinalRampSteps)
    );

    if (_outputDigipotStep > currentLimitDigipotStep) {
        _outputDigipotStep = currentLimitDigipotStep;
    } else if (_outputDigipotStep < currentLimitDigipotStep) {
        if (now - _lastStepChangeAt >= kIncreaseIntervalMs) {
            _lastStepChangeAt = now;

            if (_outputDigipotStep < acceleratedTarget) {
                const int16_t stepsPerIncrease = max(
                    static_cast<int16_t>(1),
                    static_cast<int16_t>(currentLimitDigipotStep / kRampUpDivisor)
                );
                _outputDigipotStep = min(
                    static_cast<int16_t>(_outputDigipotStep + stepsPerIncrease),
                    acceleratedTarget
                );
            } else {
                // Final steps: one step per timer interval.
                ++_outputDigipotStep;
            }
        }
    }

    writeDigipot(static_cast<uint8_t>(_outputDigipotStep));
}

void CurrentControl::minimumCurrent() {
    _outputDigipotStep = 0;
    _currentLimitDigipotStep = 0;
    _lastStepChangeAt = millis();
    writeDigipot(0);
}

float CurrentControl::getCurrentLimitA() const {
    return kCurrentAtStepZeroA + static_cast<float>(_outputDigipotStep) * kCurrentPerStepA;
}

bool CurrentControl::isRampUpFinished() const {
    return _outputDigipotStep >= _currentLimitDigipotStep;
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
