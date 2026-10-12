#include "VoltageMonitor.h"
#include "PDManager.h"

#include <algorithm>

static constexpr float kShutdownVoltageV = 42.5f;

// 12-bit ADC via 219k/4.86k divider, ADC_0db (~50.67V full scale).
static constexpr uint8_t kAdcResolutionBits  = 12;
static constexpr float kAdcVoltageFullScaleV = 50.67f;
static constexpr float kAdcResolution       = 1U << kAdcResolutionBits;

// Linear fit from ADC and meter readings spanning 29.9-45.7V.
static constexpr float kVoltageCalibrationGain   = 0.85666f;
static constexpr float kVoltageCalibrationOffset = 3.31725f;

void VoltageMonitor::begin(uint8_t adcPin, PDManager &pdManager) {
    _adcPin = adcPin;
    _pdManager = &pdManager;
    analogReadResolution(kAdcResolutionBits);
    pinMode(_adcPin, INPUT);
    // Core 3 applies per-pin attenuation only after analogRead initializes the channel.
    (void)analogRead(_adcPin);
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
        const float alpha = clampedVoltageV > _voltageV ? kEmaRiseAlpha : kEmaFallAlpha;
        _voltageV += alpha * (clampedVoltageV - _voltageV);
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
