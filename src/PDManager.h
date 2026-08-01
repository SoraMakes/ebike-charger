#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_HUSB238.h>

/// Manages the USB-PD source connection and its active power contract.
class PDManager {
public:
    PDManager() = default;

    /// Initializes the USB-PD controller on the specified I2C bus.
    /// @return true when the controller is available.
    bool begin(TwoWire &wire);

    /// Reports whether a USB-PD source is currently attached.
    bool isAttached();

    /// Restarts the controller and refreshes the source's advertised capabilities.
    void resetAndRefreshSource();

    /// Requests the highest available source voltage above 5 V.
    /// @param voltage Receives the requested voltage in volts.
    /// @param current Receives the current limit advertised for that voltage in amps.
    /// @return true when a higher-voltage source option was found and requested.
    bool requestHighestVoltagePdo(float &voltage, float &current);

    /// Reports whether the most recent power request failed.
    bool hasPDRequestFailed();

    /// Returns the voltage of the active source contract in volts.
    float getVoltage();

    /// Returns the current limit of the active source contract in amps.
    float getCurrent();

    /// Returns the power limit of the active source contract in watts.
    float getWattage();

private:
    Adafruit_HUSB238 _husb;

    static float _voltageFromActiveSetting(HUSB238_VoltageSetting vs);
    static float _ampsFromCurrentSetting(HUSB238_CurrentSetting cur);
};
