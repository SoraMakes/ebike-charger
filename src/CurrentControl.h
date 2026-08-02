#pragma once

#include <Arduino.h>
#include <Wire.h>

/// Controls the regulator enable state and open-loop charge-current setpoint.
class CurrentControl {
public:
    /// Initializes the controller on the specified I2C bus at a safe output level.
    void begin(TwoWire &wire);

    /// Applies an upper bound to the output power.
    /// The setpoint decreases immediately when it exceeds the bound and increases
    /// gradually when additional power is available.
    /// @param limitWatts Maximum permitted output power in watts.
    void update(float limitWatts);

    /// Disables the regulator and sets the digipot to step 0.
    void safe();

    /// Tests the regulator from the safe level through the requested digipot level.
    /// @param maxLevel Highest digipot level to test.
    void test(uint8_t maxLevel);

private:
    TwoWire *_wire         = nullptr;
    /// Level -1 disables the regulator; level 0 enables it at digipot step 0.
    int16_t  _powerLevel   = -1;

    void    writeDigipot(uint8_t step) const;
    static int16_t ampsToStep(float amps);
};
