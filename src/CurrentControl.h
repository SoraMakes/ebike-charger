#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "VoltageMonitor.h"

class PDManager;

/// Controls the regulator enable state and open-loop charge-current setpoint.
class CurrentControl {
public:
    /// Initializes the controller and voltage monitor at a safe output level.
    void begin(TwoWire &wire, VoltageMonitor &voltageMonitor, PDManager &pdManager);

    /// Applies an upper bound to the output power at the latest measured
    /// output voltage.
    /// The setpoint decreases immediately when it exceeds the bound and increases
    /// over time when additional power is available.
    /// @param limitWatts Maximum permitted output power in watts.
    void update(float limitWatts);

    /// Disables the regulator and sets the digipot to step 0.
    void safe();

    /// Permanently disables the regulator until reset and stores a short reason.
    void requestShutdown(const char *reason);

    /// Returns the currently configured output current limit in amps
    /// (0 when the regulator is disabled).
    float getCurrentLimitA() const;

    /// Returns true once the current setpoint has ramped up to its target
    /// (i.e. the setpoint is no longer increasing). False while disabled.
    bool isRampUpFinished() const;

    /// Returns true after a permanent shutdown request. Once latched, the
    /// regulator can never be re-enabled until reset.
    bool isShutdownLatched() const;

    /// Returns the short reason for the permanent shutdown.
    const char *getShutdownReason() const;

    /// Returns the digipot wiper step last written (-1 before the first write).
    int16_t getDigipotStep() const { return _lastDigipotStep; }

    /// Returns the internal power level (-1 = disabled) and its ramp target.
    int16_t getPowerLevel() const { return _powerLevel; }
    int16_t getTargetLevel() const { return _targetLevel; }

    /// Tests the regulator from the safe level through the requested digipot level.
    /// @param maxLevel Highest digipot level to test.
    void test(uint8_t maxLevel);

    /// Tests the regulator at a fixed digipot level.
    /// @param level Digipot level to test.
    void test_set_fixed(uint8_t level);

private:
    TwoWire *_wire         = nullptr;
    VoltageMonitor *_voltageMonitor = nullptr;
    PDManager *_pdManager  = nullptr;
    /// Level -1 disables the regulator; level 0 enables it at digipot step 0.
    int16_t  _powerLevel   = -1;
    int16_t  _targetLevel  = -1;
    int16_t  _lastDigipotStep = -1;
    uint32_t _lastIncreaseAt = 0;
    uint32_t _lastVoltageLogAt = 0;
    bool     _shutdownLatched = false;
    char     _shutdownReason[17] = "";

    void    writeDigipot(uint8_t step);
    static int16_t ampsToStep(float amps);
};
