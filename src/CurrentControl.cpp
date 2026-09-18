#include "CurrentControl.h"
#include "DiagLog.h"

#include <math.h>

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;
static constexpr uint32_t kIncreaseIntervalMs = 250;
static constexpr int16_t kFinalRampSteps = 4;
static constexpr int16_t kRampUpDivisor = 8;

// Output-current calibration: actual = 0.14A + step * 0.0572A.
static constexpr float kCurrentAtStepZeroA   = 0.14f;
static constexpr float kCurrentPerStepA      = 0.0572f;
static constexpr float kMaxAllowedOutputAmps = 3.0f;

void CurrentControl::begin(TwoWire &wire, VoltageMonitor &voltageMonitor) {
    _wire = &wire;
    _voltageMonitor = &voltageMonitor;
    _lastDigipotStep = -1;
    safe();
}

void CurrentControl::update(float limitWatts) {
    if (_shutdownLatched) {
        safe();
        return;
    }

    const uint32_t now = millis();
    const float outputVoltageV = _voltageMonitor->getVoltage();

    if (limitWatts <= 0.0f || !isfinite(outputVoltageV) || outputVoltageV <= 0.0f) {
        safe();
        return;
    }

    const float targetAmps = limitWatts / outputVoltageV;
    const float currentLimitA = min(targetAmps, kMaxAllowedOutputAmps);
    const int16_t targetLevel = ampsToStep(currentLimitA);
    _targetLevel = targetLevel;

    if (targetLevel < 0) {
        safe();
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

    // -1 disabled; step 0 is the safe digipot value.
    const uint8_t step = _powerLevel < 0
        ? 0
        : static_cast<uint8_t>(_powerLevel);
    writeDigipot(step);
}

void CurrentControl::safe() {
    _powerLevel = -1;
    _targetLevel = -1;
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
    DiagLog::event("safety %s", _shutdownReason);
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
