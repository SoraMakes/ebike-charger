#pragma once

#include <Arduino.h>

/// Regulator efficiency estimate from allowed vs measured input power.
class EfficiencyEstimator {
public:
    void begin();
    void update(float allowedRegulatorInputW, float actualRegulatorInputW,
                bool rampUpFinished, bool noLoad);
    float getEfficiency() const;

private:
    float _efficiency = kDefaultEfficiency;

    static constexpr float kDefaultEfficiency = 0.88f;
    static constexpr float kMinEfficiency     = 0.85f;
    static constexpr float kMaxEfficiency     = 0.95f;
    static constexpr float kEfficiencyStep    = 0.005f; // Increase-only.
    static constexpr float kTargetUtilization = 0.98f;  // Target utilization.
};