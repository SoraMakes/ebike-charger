#pragma once

#include <Arduino.h>

class PDManager;

/// Output voltage via analog divider.
class VoltageMonitor {
public:
    /// Configure ADC input and associate the manager used for overvoltage shutdown.
    void begin(uint8_t adcPin, PDManager &pdManager);
    /// Sample and filter output voltage, then shut down on sustained overvoltage.
    void update();
    /// Return the filtered, calibrated output voltage in volts.
    float getVoltage() const;

private:
    static constexpr float kEmaRiseAlpha = 0.5f;
    static constexpr float kEmaFallAlpha = 0.1f;
    static constexpr float kEmaResetVoltageV = 5.5f;
    // Calibration fit floor for the measured output voltage.
    static constexpr float kCalibrationValidFromV = 2.0f;
    static constexpr uint8_t kOvervoltageRequiredSamples = 20;

    uint8_t _adcPin;
    float   _voltageV   = 0.0f;
    uint8_t _overvoltageCount = 0;
    PDManager *_pdManager = nullptr;

    /// Read three ADC samples and return the median to reject outliers.
    int readMedianAdc() const;
};
