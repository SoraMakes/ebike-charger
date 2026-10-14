#include "CurrentControl.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;
static constexpr uint32_t kRampStepIntervalMs = 75;
static constexpr uint32_t kFinalRampStepIntervalMs = 500;
static constexpr int16_t kFinalRampSlowdownSteps = 3;

// Output-current calibration.
static constexpr float kCurrentAtStepZeroA   = 0.13819f;
static constexpr float kCurrentPerStepA      = 0.0605312f;
// Absolute firmware cap; update() also applies the mode-selected limit.
static constexpr float kMaxAllowedOutputAmps = 5.0f;

// No-load hold step (~0.37A).
static constexpr int16_t kNoLoadHoldStep = 4;

void CurrentControl::begin(TwoWire &wire, VoltageMonitor &voltageMonitor) {
    _wire = &wire;
    _voltageMonitor = &voltageMonitor;
    _lastDigipotStep = -1;
    minimumCurrent();
}

void CurrentControl::update(float limitWatts, bool noLoad, float maximumCurrentA) {
    const uint32_t now = millis();
    const float outputVoltageV = _voltageMonitor->getVoltage();

    if (limitWatts <= 0.0f || !isfinite(outputVoltageV) || outputVoltageV <= 0.0f) {
        minimumCurrent();
        return;
    }

    const float targetAmps = limitWatts / outputVoltageV;
    const float currentLimitA = min(targetAmps, min(maximumCurrentA, kMaxAllowedOutputAmps));
    const int16_t currentLimitDigipotStep = ampsToStep(currentLimitA);
    _currentLimitDigipotStep = currentLimitDigipotStep;

    // During no-load periods, hold the output at a low setpoint until load rises.
    if (noLoad && currentLimitDigipotStep > kNoLoadHoldStep) {
        _outputDigipotStep = kNoLoadHoldStep;
        _lastStepChangeAt = now;
        _finalRampIncreasePending = false;
        writeDigipot(static_cast<uint8_t>(kNoLoadHoldStep));
        return;
    }

    updateRamp(now);
    writeDigipot(static_cast<uint8_t>(_outputDigipotStep));
}

void CurrentControl::updateRamp(uint32_t now) {
    // Apply lower targets immediately and cancel any pending slow-ramp wait.
    if (_outputDigipotStep >= _currentLimitDigipotStep) {
        _outputDigipotStep = _currentLimitDigipotStep;
        _finalRampIncreasePending = false;
        return;
    }

    const int16_t remainingSteps = _currentLimitDigipotStep - _outputDigipotStep;
    const bool slowRamp = remainingSteps <= kFinalRampSlowdownSteps;
    if (slowRamp) {
        if (!_finalRampIncreasePending) {
            // Start the slower cadence when demand first reaches the final steps.
            _finalRampRequestAt = now;
            _finalRampIncreasePending = true;
            return;
        }
        // Wait until the full final-step interval has elapsed.
        if (now - _finalRampRequestAt < kFinalRampStepIntervalMs) {
            return;
        }

        // Start a fresh slow interval after each final-step increase.
        _finalRampRequestAt = now;
        // Fall through to apply the step below.
    } else {
        _finalRampIncreasePending = false;
        if (now - _lastStepChangeAt < kRampStepIntervalMs) {
            return;
        }
    }

    _lastStepChangeAt = now;
    ++_outputDigipotStep;
}

void CurrentControl::minimumCurrent() {
    _outputDigipotStep = 0;
    _currentLimitDigipotStep = 0;
    _lastStepChangeAt = millis();
    _finalRampIncreasePending = false;
    writeDigipot(0);
}

float CurrentControl::getCurrentLimitA() const {
    return kCurrentAtStepZeroA + static_cast<float>(_outputDigipotStep) * kCurrentPerStepA;
}

bool CurrentControl::isRampUpFinished() const {
    // Wait for the digipot to acknowledge the requested target step.
    return _outputDigipotStep >= _currentLimitDigipotStep && _lastDigipotStep == _outputDigipotStep;
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
