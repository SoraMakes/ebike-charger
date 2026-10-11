#pragma once

#include <Arduino.h>
#include <Adafruit_BMP280.h>

class PDManager;

/// Temperature sensing, fan control, and thermal power derating.
class ThermalManager {
public:
    /// Initialize fan PWM and the temperature sensor.
    void begin(uint8_t fanPin, PDManager &pdManager);
    /// Sample temperature, update fan speed and power limit, and shut down on sensor or temperature faults.
    void update();
    /// Enable the fan; it remains enabled afterward.
    void enableFan();
    /// Return the most recently accepted temperature reading in Celsius.
    float getTemperatureC() const;
    /// Return the thermal output-power limit factor, from 1.0 to 0.0.
    float getOutputPowerLimitFactor() const;
    /// Return true if sensor initialization or a temperature reading failed.
    bool hasSensorFault() const;

private:
    Adafruit_BMP280 _sensor;
    float _temperatureC = 25.0f;
    float _outputPowerLimitFactor = 0.0f;
    bool _sensorFault = true;
    bool _shutdownLatched = false;
    bool _fanEnabled = false;
    PDManager *_pdManager = nullptr;

    void applyFanPwm(uint16_t duty) const;
    uint16_t calculateFanDuty(float temperatureC) const;
    void enterShutdown(const char *reason);
};
