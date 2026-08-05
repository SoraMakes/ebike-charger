#include "VoltageMonitor.h"

// ESP32 (TTGO T-Display) ADC: 12-bit, 0..4095 counts.
// The divider maps ~51.86 V full scale to the ADC input range.
static constexpr float kAdcVoltageFullScaleV = 51.86f;
static constexpr float kAdcResolution       = 4096.0f;

// Endpoint correction: +0.3 V at 30 V and -0.3 V at 41 V.
static constexpr float kVoltageCalibrationGain   = 1.01854f;
static constexpr float kVoltageCalibrationOffset = 1.93636f;

// ESP8266 (D1 mini) original values, kept for reference:
// static constexpr float kAdcVoltageFullScaleV = 51.86f;
// static constexpr float kAdcResolution       = 1024.0f;
// static constexpr float kVoltageCalibrationGain = 0.980;

void VoltageMonitor::begin(uint8_t adcPin) {
    _adcPin = adcPin;
    pinMode(_adcPin, INPUT);
    // ESP32: use the widest input range (11 dB) for the ~50 V divider.
    analogSetPinAttenuation(_adcPin, ADC_11db);
}

void VoltageMonitor::update() {
    _rawReading = analogRead(_adcPin);
    const float dividerVoltageV = static_cast<float>(_rawReading) *
                                  (kAdcVoltageFullScaleV / kAdcResolution);
    _voltageV = dividerVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}

int VoltageMonitor::getRawReading() const {
    return _rawReading;
}