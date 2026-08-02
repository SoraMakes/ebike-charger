#include "VoltageMonitor.h"

// Initial divider conversion: 51.86 V full scale across 1024 ADC counts.
static constexpr float kAdcVoltageFullScaleV = 51.86f;
static constexpr float kAdcResolution       = 1024.0f;

// Gain calibration based on 29.725 V -> 29.9 V and 41.50 V -> 41.8 V.
static constexpr float kVoltageCalibrationGain = 0.980;

void VoltageMonitor::begin(uint8_t adcPin) {
    _adcPin = adcPin;
    pinMode(_adcPin, INPUT);
}

void VoltageMonitor::update() {
    _rawReading = analogRead(_adcPin);
    const float dividerVoltageV = static_cast<float>(_rawReading) *
                                  (kAdcVoltageFullScaleV / kAdcResolution);
    _voltageV = dividerVoltageV * kVoltageCalibrationGain;
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}

int VoltageMonitor::getRawReading() const {
    return _rawReading;
}