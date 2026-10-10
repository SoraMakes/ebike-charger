#pragma once

#include <Arduino.h>
#include <Adafruit_BMP280.h>

class PDManager;

/// Temperature sensing plus fan and thermal power limit.
class ThermalManager {
public:
    /// Initialize fan PWM and the temperature sensor.
    void begin(uint8_t fanPin, PDManager &pdManager);
    /// Sample temperature, update fan speed and power limit, and shut down on sensor or temperature faults.
    void update();
    /// Return the most recently accepted temperature reading in Celsius.
    float getTemperatureC() const;
    /// Return the current thermally allowed output power in watts.
    float getOutputPowerLimitW() const;
    /// Return true if sensor initialization or a temperature reading failed.
    bool hasSensorFault() const;

private:
    Adafruit_BMP280 _sensor;
    float _temperatureC = 25.0f;
    float _outputPowerLimitW = 0.0f;
    bool _sensorFault = true;
    bool _shutdownLatched = false;
    PDManager *_pdManager = nullptr;

    void applyFanPwm(uint16_t duty) const;
    uint16_t calculateFanDuty(float temperatureC) const;
    float calculateOutputPowerLimit(float temperatureC) const;
    void enterShutdown(const char *reason);
};
