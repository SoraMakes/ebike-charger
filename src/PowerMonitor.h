#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <INA238.h>

/// Measures USB-PD input voltage/current via the RotoPD Pro's onboard INA238.
class PowerMonitor {
public:
    /// Initializes the INA238 (I2C address 0x40, 5 mOhm shunt on RotoPD Pro).
    /// @return true when the INA238 is available.
    bool  begin();

    /// Refreshes the latest input voltage and current measurements.
    void  update();

    /// Returns the most recently measured input bus voltage in volts.
    float getInputVoltage() const;

    /// Returns the most recently measured input current in amps.
    float getInputCurrent() const;

private:
    INA238 _ina{0x40};
    float _busVoltage = 0.0f;
    float _current    = 0.0f;
};
