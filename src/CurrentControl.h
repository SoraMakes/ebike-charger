#pragma once

#include <Arduino.h>
#include <Wire.h>

/// Controls the open-loop charge-current setpoint through the MCP4018.
class CurrentControl {
public:
    /// Initializes the controller on the specified I2C bus at a safe output level.
    void begin(TwoWire &wire);

    /// Applies an upper bound to the output power.
    /// The setpoint decreases immediately when it exceeds the bound and increases
    /// gradually when additional power is available.
    /// @param limitWatts Maximum permitted output power in watts.
    void update(float limitWatts);

    /// Immediately sets the charge-current output to its safe minimum.
    void safe();

private:
    TwoWire *_wire         = nullptr;
    uint8_t  _step         = 0;

    void    writeDigipot(uint8_t step) const;
    static uint8_t ampsToStep(float amps);
};
