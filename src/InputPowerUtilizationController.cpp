#include "InputPowerUtilizationController.h"

#include <math.h>

void InputPowerUtilizationController::begin() {
    _outputPowerFactor = kInitialOutputPowerFactor;
}

void InputPowerUtilizationController::update(float availableRegulatorInputW,
                                             float measuredRegulatorInputW,
                                             bool rampUpFinished, bool noLoad) {
    if (!isfinite(availableRegulatorInputW) || availableRegulatorInputW <= 0.0f) {
        return;
    }

    if (noLoad) {
        _outputPowerFactor = kInitialOutputPowerFactor;
        return;
    }

    if (!isfinite(measuredRegulatorInputW) || measuredRegulatorInputW <= 0.0f) {
        return;
    }

    const float utilization = measuredRegulatorInputW / availableRegulatorInputW;
    if (utilization > kTargetUtilization) {
        _outputPowerFactor *= kTargetUtilization / utilization;
    } else if (rampUpFinished && _outputPowerFactor < kMaxOutputPowerFactor) {
        _outputPowerFactor = min(kMaxOutputPowerFactor,
                                 _outputPowerFactor + kFactorIncrease);
    }
}

float InputPowerUtilizationController::getOutputPowerFactor() const {
    return _outputPowerFactor;
}
