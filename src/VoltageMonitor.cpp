#include "VoltageMonitor.h"
#include "PDManager.h"

#include <algorithm>

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
    const int rawReading = readMedianAdc();

    const float measuredVoltageV = static_cast<float>(rawReading) * (kAdcVoltageFullScaleV / kAdcResolution);

    // Correct the measured voltage using the calibration fit.
    const float calibratedVoltageV = measuredVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;
    // Reject measurements below the fit's valid range.
    const float clampedVoltageV = measuredVoltageV < kCalibrationValidFromV ? 0.0f : calibratedVoltageV;

    // Bypass smoothing near the low-voltage threshold so startup and dropout respond promptly.
    const bool resetEma = clampedVoltageV < kEmaResetVoltageV || _voltageV < kEmaResetVoltageV;
    if (resetEma) {
        _voltageV = clampedVoltageV;
    } else {
        _voltageV += kEmaAlpha * (clampedVoltageV - _voltageV);
    }

    // Shut down when consecutive readings confirm overvoltage.
    const bool overVoltage = _voltageV >= kShutdownVoltageV;
    _overvoltageCount = overVoltage ? _overvoltageCount + 1 : 0;
    if (_overvoltageCount >= kOvervoltageRequiredSamples) {
        _pdManager->shutdown("overvoltage");
    }
}

int VoltageMonitor::readMedianAdc() const {
    int readings[3] = {analogRead(_adcPin), analogRead(_adcPin), analogRead(_adcPin)};
    std::sort(readings, readings + 3);
    return readings[1];
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}
