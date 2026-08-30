#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

class CurrentControl;

/// Measures ambient temperature and manages thermal fan and charge limits.
class ThermalManager {
public:
    /// Initializes the BMP280 and fan output.
    /// @return true when the temperature sensor was detected.
    bool begin(TwoWire &wire, uint8_t fanPin, CurrentControl &currentControl);

    /// Updates the temperature state and fan output.
    /// Call periodically, approximately once per second.
    void update();

    /// Returns the most recent temperature in degrees Celsius.
    float getTemperatureC() const;

    /// Returns the thermally permitted output power in watts, from 0 to 100.
    float getOutputPowerLimitW() const;

    /// Returns true when the sensor is unavailable or a reading failed.
    bool hasSensorFault() const;

    /// Returns true after an overtemperature or sensor-fault shutdown.
    bool isShutdownLatched() const;

private:
    static constexpr uint16_t kPwmRange = 1023;
    static constexpr uint32_t kFanPwmFrequencyHz = 16000;

    Adafruit_BMP280 _sensor;
    uint8_t _fanPin = 0;
    float _temperatureC = 25.0f;
    float _outputPowerLimitW = 0.0f;
    bool _sensorFault = true;
    bool _shutdownLatched = false;
    CurrentControl *_currentControl = nullptr;

    void applyFanPwm(uint16_t duty) const;
    void enterShutdown(const char *reason);
};
