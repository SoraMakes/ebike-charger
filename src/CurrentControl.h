#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "VoltageMonitor.h"

/// Regulator charge-current setpoint via digipot.
class CurrentControl {
public:
    void begin(TwoWire &wire, VoltageMonitor &voltageMonitor);

    /// Upper output-power bound at latest output voltage.
    void update(float limitWatts);

    /// Disable output, digipot to step 0.
    void safe();

    /// Latch permanent shutdown with reason.
    void requestShutdown(const char *reason);

    /// Configured current limit, 0 when disabled.
    float getCurrentLimitA() const;

    /// True once setpoint reached its target.
    bool isRampUpFinished() const;

    /// True after permanent shutdown.
    bool isShutdownLatched() const;

    /// Shutdown reason.
    const char *getShutdownReason() const;

    /// Last digipot step written (-1 before first write).
    int16_t getDigipotStep() const { return _lastDigipotStep; }

private:
    TwoWire *_wire         = nullptr;
    VoltageMonitor *_voltageMonitor = nullptr;
    int16_t  _powerLevel   = -1;
    int16_t  _targetLevel  = -1;
    int16_t  _lastDigipotStep = -1;
    uint32_t _lastIncreaseAt = 0;
    bool     _shutdownLatched = false;
    char     _shutdownReason[17] = "";

    void    writeDigipot(uint8_t step);
    static int16_t ampsToStep(float amps);
};
