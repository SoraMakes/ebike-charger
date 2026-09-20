#pragma once

#include <Arduino.h>

class PDManager;

/// Output voltage via analog divider.
class VoltageMonitor {
public:
    void begin(uint8_t adcPin, PDManager &pdManager);
    void update();
    float getVoltage() const;

private:
    static constexpr float kEmaAlpha = 0.1f;
    static constexpr float kEmaResetVoltageV = 5.5f;
    // Fit floor: report 0V below this divider voltage.
    static constexpr float kCalibrationValidFromV = 2.0f;
    static constexpr uint8_t kOvervoltageRequiredSamples = 20;

    uint8_t _adcPin = 33;
    int     _rawReading = 0;
    float   _voltageV   = 0.0f;
    uint8_t _overvoltageCount = 0;
    PDManager *_pdManager = nullptr;
};