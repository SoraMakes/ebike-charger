#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <INA238.h>

/// Monitor input voltage, current, and latched fault alerts with the onboard INA238.
class PowerMonitor {
public:
    /// Initialize INA238 measurement and alert configuration.
    /// @return true if the sensor initializes successfully.
    bool begin();
    /// Refresh readings, latch alerts, and detect INA read failures.
    void update();
    /// Return the most recently sampled input voltage in volts.
    float getInputVoltage() const;
    /// Return the most recently sampled input current in amperes.
    float getInputCurrent() const;
    /// Return the most recently sampled INA238 die temperature in Celsius.
    float getTemperatureC() const;
    /// Return true if any monitored alert has been latched.
    bool hasAlert() const { return _alertSticky != 0; }
    /// Return true after an INA initialization or read failure.
    bool hasSensorFault() const { return _sensorFault; }
    /// Return names for recorded alerts, or an empty string if none; the buffer is reused on each call.
    const char *getAlertText() const;

private:
    bool checkReadError(const char *measurement, bool validReading = true);

    INA238 _ina{0x40};
    float _busVoltage = 0.0f;
    float _current    = 0.0f;
    float _temperatureC = 25.0f;
    uint16_t _alertSticky = 0;
    uint8_t _consecutiveReadErrors = 0;
    bool _sensorFault = false;
};
