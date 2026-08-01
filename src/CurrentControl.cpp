#include "CurrentControl.h"

static constexpr uint8_t kMCP4018Address = 0x2F; // hardware-fixed

static constexpr uint8_t kMaxStep = 127;

// Measured between the wiper and one end: 125 ohms at step 0,
// approximately 35.8 ohms per step, and 4.67 kohms at step 127.
static constexpr float kMinResistanceOhms      = 125.0f;
static constexpr float kMaxResistanceOhms      = 4670.0f;
static constexpr float kRegulatorMaxOutputAmps = 15.0f;
static constexpr float kMaxAllowedOutputAmps   = 3.0f;  // Software safety limit
static constexpr float kAssumedOutputVoltageV  = 42.0f;  // TODO: get actual voltage, 42V is the max charging voltage
static constexpr float kResistanceRangeOhms    = kMaxResistanceOhms - kMinResistanceOhms;
static constexpr float kResistancePerStep      = kResistanceRangeOhms / kMaxStep;

void CurrentControl::begin(TwoWire &wire) {
    _wire = &wire;
    safe();
}

void CurrentControl::update(float limitWatts) {
    if (limitWatts <= 0.0f) {
        safe();
        return;
    }

    const float targetAmps = limitWatts / kAssumedOutputVoltageV;
    const uint8_t targetStep = ampsToStep(min(targetAmps, kMaxAllowedOutputAmps));

    if (_step > targetStep) {
        _step = targetStep;
    } else if (_step < targetStep) {
        ++_step;
    }

    writeDigipot(_step);
}

void CurrentControl::safe() {
    _step = 0;
    writeDigipot(0);
}

void CurrentControl::writeDigipot(uint8_t step) const {
    _wire->beginTransmission(kMCP4018Address);
    _wire->write(step);
    _wire->endTransmission();
}

uint8_t CurrentControl::ampsToStep(float amps) {
    const float clampedAmps = constrain(amps, 0.0f, kRegulatorMaxOutputAmps);
    const float targetResistance = kMinResistanceOhms + clampedAmps / kRegulatorMaxOutputAmps * kResistanceRangeOhms;
    const int step = (int)((targetResistance - kMinResistanceOhms) / kResistancePerStep);
    return (uint8_t)constrain(step, 0, (int)kMaxStep);
}
