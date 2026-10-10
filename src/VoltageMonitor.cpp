#include "VoltageMonitor.h"
#include "PDManager.h"

static constexpr float kShutdownVoltageV = 42.5f;

// 12-bit ADC via 219k/4.86k divider, ADC_0db (~50.67V full scale).
static constexpr float kAdcVoltageFullScaleV = 50.67f;
static constexpr float kAdcResolution       = 4096.0f;

// 30-41V fit.
static constexpr float kVoltageCalibrationGain   = 0.854f;
static constexpr float kVoltageCalibrationOffset = 1.73636f;

void VoltageMonitor::begin(uint8_t adcPin, PDManager &pdManager) {
    _adcPin = adcPin;
    _pdManager = &pdManager;
    pinMode(_adcPin, INPUT);
    analogSetPinAttenuation(_adcPin, ADC_0db);
}

void VoltageMonitor::update() {
    // Outlier rejection.
    int readings[3];
    readings[0] = analogRead(_adcPin);
    readings[1] = analogRead(_adcPin);
    readings[2] = analogRead(_adcPin);
    if (readings[0] > readings[1]) { int t = readings[0]; readings[0] = readings[1]; readings[1] = t; }
    if (readings[1] > readings[2]) { int t = readings[1]; readings[1] = readings[2]; readings[2] = t; }
    if (readings[0] > readings[1]) { int t = readings[0]; readings[0] = readings[1]; readings[1] = t; }
    _rawReading = readings[1];

    const float dividerVoltageV = static_cast<float>(_rawReading) * (kAdcVoltageFullScaleV / kAdcResolution);

    // Clamp invalid near-zero fit.
    const float calibratedVoltageV = (dividerVoltageV < kCalibrationValidFromV)
        ? 0.0f
        : dividerVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;

    // Low-voltage reset.
    if (calibratedVoltageV < kEmaResetVoltageV || _voltageV < kEmaResetVoltageV) {
        _voltageV = calibratedVoltageV;
    } else {
        _voltageV += kEmaAlpha * (calibratedVoltageV - _voltageV);
    }

    // Sustained-overvoltage shutdown.
    const bool overVoltage = _voltageV >= kShutdownVoltageV;
    _overvoltageCount = overVoltage ? _overvoltageCount + 1 : 0;
    if (_overvoltageCount >= kOvervoltageRequiredSamples) {
        _pdManager->shutdown("overvoltage");
    }
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}