#pragma once

#include <Arduino.h>

/// Estimates the voltage-regulator efficiency by comparing the actual
/// measured input power against the allowed input power budget.
class EfficiencyEstimator {
public:
    /// Resets the estimate to its default baseline.
    void begin();

    /// Adjusts the estimate. Call periodically (e.g. every 50 ms).
    /// @param allowedRegulatorInputW Max input power the PD contract allows,
    ///                               minus the constant electronics draw.
    /// @param actualRegulatorInputW  Measured input power right now, minus the
    ///                               constant electronics draw.
    /// @param rampUpFinished         True once the output current reached its target.
    void update(float allowedRegulatorInputW, float actualRegulatorInputW, bool rampUpFinished);

    /// Returns the current efficiency estimate.
    float getEfficiency() const;

private:
    float _efficiency = kDefaultEfficiency;

    static constexpr float kDefaultEfficiency = 0.88f;
    static constexpr float kMinEfficiency     = 0.85f;
    static constexpr float kMaxEfficiency     = 0.95f;
    static constexpr float kEfficiencyStep    = 0.005f; // per tick, increase only
    static constexpr float kTargetUtilization = 0.98f;  // target input-power utilization
};