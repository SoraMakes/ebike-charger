#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

class PDManager;

/// Temperature sensing plus fan and thermal power limit.
class ThermalManager {
public:
    bool begin(TwoWire &wire, uint8_t fanPin, PDManager &pdManager);
    void update();
    float getTemperatureC() const;
    float getOutputPowerLimitW() const;
    bool hasSensorFault() const;
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
    PDManager *_pdManager = nullptr;

    void applyFanPwm(uint16_t duty) const;
    void enterShutdown(const char *reason);
};
