#include "VoltageMonitor.h"
#include "CurrentControl.h"

static constexpr float kShutdownVoltageV = 42.5f;

// 12-bit ADC via 219k/4.86k divider, ADC_0db (~50.67V full scale).
static constexpr float kAdcVoltageFullScaleV = 50.67f;
static constexpr float kAdcResolution       = 4096.0f;

// Straight-line fit from 30-41V range.
static constexpr float kVoltageCalibrationGain   = 0.854f;
static constexpr float kVoltageCalibrationOffset = 1.73636f;

void VoltageMonitor::begin(uint8_t adcPin, CurrentControl &currentControl) {
    _adcPin = adcPin;
    _currentControl = &currentControl;
    pinMode(_adcPin, INPUT);
    // ESP32: no internal voltage divider.
    analogSetPinAttenuation(_adcPin, ADC_0db);
}

void VoltageMonitor::update() {
    // Median of 3 rejects outliers.
    int readings[3];
    readings[0] = analogRead(_adcPin);
    readings[1] = analogRead(_adcPin);
    readings[2] = analogRead(_adcPin);
    // Simple sort to find the median.
    if (readings[0] > readings[1]) { int t = readings[0]; readings[0] = readings[1]; readings[1] = t; }
    if (readings[1] > readings[2]) { int t = readings[1]; readings[1] = readings[2]; readings[2] = t; }
    if (readings[0] > readings[1]) { int t = readings[0]; readings[0] = readings[1]; readings[1] = t; }
    _rawReading = readings[1];

    const float dividerVoltageV = static_cast<float>(_rawReading) * (kAdcVoltageFullScaleV / kAdcResolution);

    // Fit invalid near zero (would report +1.74V at raw 0): clamp to 0.
    const float calibratedVoltageV = (dividerVoltageV < kCalibrationValidFromV)
        ? 0.0f
        : dividerVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;

    // Restart EMA from low voltages.
    if (calibratedVoltageV < kEmaResetVoltageV || _voltageV < kEmaResetVoltageV) {
        _voltageV = calibratedVoltageV;
    } else {
        _voltageV += kEmaAlpha * (calibratedVoltageV - _voltageV);
    }

    // Latch shutdown after sustained overvoltage.
    const bool overVoltage = _voltageV >= kShutdownVoltageV;
    _overvoltageCount = overVoltage ? _overvoltageCount + 1 : 0;
    if (_overvoltageCount >= kOvervoltageRequiredSamples) {
        _currentControl->requestShutdown("overvoltage");
    }
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}