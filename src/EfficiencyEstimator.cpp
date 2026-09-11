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
        Serial.printf("[EFFICIENCY] high draw (%.0f%%); dropped to %.2f\n", utilization * 100.0f, _efficiency);
    } else if (utilization < kTargetUtilization && rampUpFinished &&_efficiency < kMaxEfficiency) {
        _efficiency = min(kMaxEfficiency, _efficiency + kEfficiencyStep);
        Serial.printf("[EFFICIENCY] headroom (%.0f%%); raised to %.2f\n", utilization * 100.0f, _efficiency);
    }
}

float EfficiencyEstimator::getEfficiency() const {
    return _efficiency;
}