#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "VoltageMonitor.h"

/// Regulator charge-current setpoint via digipot.
class CurrentControl {
public:
    void begin(TwoWire &wire, VoltageMonitor &voltageMonitor);

    /// Output-power bound; noLoad holds the setpoint low until load rises.
    void update(float limitWatts, bool noLoad);

    /// Drop to minimum-current setpoint, digipot to step 0 (~0.14 A).
    /// Does NOT turn the output off: no VR enable exists, step 0 still
    /// sources ~0.14 A. Permanent cut-out is PDManager::shutdown().
    void minimumCurrent();

    /// Configured current limit; floor is step 0 (~0.14 A), never 0.
    float getCurrentLimitA() const;

    /// True once setpoint reached its target (also true when parked at 0).
    bool isRampUpFinished() const;

    /// Last digipot step written (-1 before first write).
    int16_t getDigipotStep() const { return _lastDigipotStep; }

private:
    TwoWire *_wire         = nullptr;
    VoltageMonitor *_voltageMonitor = nullptr;
    int16_t  _powerLevel   = 0;
    int16_t  _targetLevel  = 0;
    int16_t  _lastDigipotStep = -1;
    uint32_t _lastStepChangeAt = 0;

    void    writeDigipot(uint8_t step);
    static int16_t ampsToStep(float amps);
};
