#pragma once

#include <Arduino.h>

/// Manages the thermal safety fan.
class ThermalManager {
public:
    /// Initializes the fan output.
    /// @param fanPin Output connected to the fan driver.
    void begin(uint8_t fanPin);

    /// Reapplies the fixed safety-fan output.
    ///
    /// The current implementation intentionally does not read temperature.
    /// Temperature-dependent control will be added once the NTC input and
    /// thermal limits are defined.
    void  update();

private:
    uint8_t _fanPin = 0;

    void  applyFanPwm() const;
};
