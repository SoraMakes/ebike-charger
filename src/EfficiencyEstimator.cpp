#include "EfficiencyEstimator.h"

#include <math.h>

void EfficiencyEstimator::begin() {
    _efficiency = kDefaultEfficiency;
}

void EfficiencyEstimator::update(float allowedRegulatorInputW, float actualRegulatorInputW,
                                 bool rampUpFinished, bool noLoad) {
    if (!isfinite(allowedRegulatorInputW) || allowedRegulatorInputW <= 0.0f) {
        return;
    }
    if (!isfinite(actualRegulatorInputW) || actualRegulatorInputW <= 0.0f) {
        return;
    }

    // No-load hold.
    if (noLoad) {
        _efficiency = kMinEfficiency;
        return;
    }

    const float utilization = actualRegulatorInputW / allowedRegulatorInputW;

    if (utilization > kTargetUtilization && _efficiency > kMinEfficiency) {
        _efficiency = kMinEfficiency;
    } else if (utilization < kTargetUtilization && rampUpFinished &&_efficiency < kMaxEfficiency) {
        _efficiency = min(kMaxEfficiency, _efficiency + kEfficiencyStep);
    }
}

float EfficiencyEstimator::getEfficiency() const {
    return _efficiency;
}