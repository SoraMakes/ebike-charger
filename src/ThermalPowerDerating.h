#pragma once

namespace ThermalPowerDerating {
static constexpr float kDeratingTemperatureC = 40.0f;
static constexpr float kShutdownTemperatureC = 50.0f;

inline float limitFactorForTemperatureC(float temperatureC) {
    if (temperatureC <= kDeratingTemperatureC) {
        return 1.0f;
    }
    if (temperatureC >= kShutdownTemperatureC) {
        return 0.0f;
    }

    return (kShutdownTemperatureC - temperatureC) /
        (kShutdownTemperatureC - kDeratingTemperatureC);
}
}  // namespace ThermalPowerDerating
