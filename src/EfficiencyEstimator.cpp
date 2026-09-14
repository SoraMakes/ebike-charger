#include "EfficiencyEstimator.h"

#include <math.h>

void EfficiencyEstimator::begin() {
    _efficiency = kDefaultEfficiency;
}

void EfficiencyEstimator::update(float allowedRegulatorInputW, float actualRegulatorInputW,
                                 bool rampUpFinished) {
    if (!isfinite(actualRegulatorInputW) || actualRegulatorInputW <= 0.0f) {
        return;
    }
    if (!isfinite(allowedRegulatorInputW) || allowedRegulatorInputW <= 0.0f) {
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