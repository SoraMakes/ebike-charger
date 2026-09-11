#pragma once

#include <Arduino.h>

class CurrentControl;

/// Measures charger output voltage through an analog voltage divider.
class VoltageMonitor {
public:
    /// Initializes the ADC input connected to the voltage divider.
    void begin(uint8_t adcPin, CurrentControl &currentControl);

    /// Refreshes the raw ADC reading and converted voltage values.
    void update();

    /// Returns the most recently measured, filtered voltage.
    float getVoltage() const;

private:
    static constexpr float kEmaAlpha = 0.1f; // 100 ms sampling interval
    static constexpr float kEmaResetVoltageV = 5.5f;
    // Below this raw divider voltage the calibration fit is out of range and
    // the reading is reported as 0 V rather than as its +1.74 V intercept.
    static constexpr float kCalibrationValidFromV = 2.0f;
    // Number of consecutive over-limit readings required before a shutdown is latched.
    static constexpr uint8_t kOvervoltageRequiredSamples = 20;

    uint8_t _adcPin = 33;  // TTGO T-Display voltage-divider ADC pin
    int     _rawReading = 0;
    float   _voltageV   = 0.0f;
    uint8_t _overvoltageCount = 0;
    CurrentControl *_currentControl = nullptr;
};