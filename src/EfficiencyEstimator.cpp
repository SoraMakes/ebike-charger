#include "EfficiencyEstimator.h"

void EfficiencyEstimator::begin() {
    _efficiency = kDefaultEfficiency;
}

void EfficiencyEstimator::update(float allowedRegulatorInputW, float actualRegulatorInputW,
                                 bool rampUpFinished, bool noLoad) {
    if (allowedRegulatorInputW <= 0.0f) {
        return;
    }

    // No-load: hold minimum so the budget is not inflated.
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