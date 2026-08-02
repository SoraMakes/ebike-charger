#pragma once

#include <Arduino.h>

/// Measures charger output voltage through an analog voltage divider.
class VoltageMonitor {
public:
    /// Initializes the ADC input connected to the voltage divider.
    void begin(uint8_t adcPin);

    /// Refreshes the raw ADC reading and converted voltage values.
    void update();

    /// Returns the most recently measured voltage after calibration.
    float getVoltage() const;

    /// Returns the most recently sampled ADC count.
    int getRawReading() const;

private:
    uint8_t _adcPin = A0;
    int     _rawReading = 0;
    float   _voltageV   = 0.0f;
};