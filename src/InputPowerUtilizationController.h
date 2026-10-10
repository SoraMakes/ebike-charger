#pragma once

#include <Arduino.h>

/// Adjust output power demand to approach the available PD input-power budget.
class InputPowerUtilizationController {
public:
    void begin();
    void update(float availableRegulatorInputW, float measuredRegulatorInputW,
                bool rampUpFinished, bool noLoad);
    float getOutputPowerFactor() const;

private:
    float _outputPowerFactor = kInitialOutputPowerFactor;

    static constexpr float kInitialOutputPowerFactor = 0.80f;
    static constexpr float kMaxOutputPowerFactor = 1.0f;
    // Step added to the output-power factor while utilization is below target.
    static constexpr float kFactorIncrease = 0.005f;
    static constexpr float kTargetUtilization = 0.98f;
};
