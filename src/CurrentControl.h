#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "VoltageMonitor.h"

/// Regulator charge-current setpoint via digipot.
class CurrentControl {
public:
    void begin(TwoWire &wire, VoltageMonitor &voltageMonitor);

    /// Output-power bound; noLoad holds low until load rises.
    void update(float limitWatts, bool noLoad);

    /// Minimum-current setpoint; still sources ~0.14 A.
    /// Permanent cut-out is PDManager::shutdown().
    void minimumCurrent();

    /// Current limit; floor step 0 (~0.14 A), never 0.
    float getCurrentLimitA() const;

    /// Setpoint at target.
    bool isRampUpFinished() const;

    /// Last digipot step written.
    int16_t getDigipotStep() const { return _lastDigipotStep; }

private:
    TwoWire *_wire         = nullptr;
    VoltageMonitor *_voltageMonitor = nullptr;
    int16_t  _outputDigipotStep = 0;  // Current output setpoint in digipot steps.
    int16_t  _currentLimitDigipotStep = 0;
    int16_t  _lastDigipotStep = -1;  // Last step acknowledged on I2C; -1 forces the initial write.
    uint32_t _lastStepChangeAt = 0;

    void    writeDigipot(uint8_t step);
    static int16_t ampsToStep(float amps);
};
