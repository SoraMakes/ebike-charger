#include "VoltageMonitor.h"
#include "CurrentControl.h"

static constexpr float kShutdownVoltageV = 42.5f;

// ESP32 (TTGO T-Display) ADC: 12-bit, 0..4095 counts.
// The divider maps ~51.86 V full scale to the ADC input range.
static constexpr float kAdcVoltageFullScaleV = 51.86f;
static constexpr float kAdcResolution       = 4096.0f;

// Endpoint correction: +0.3 V at 30 V and -0.3 V at 41 V.
static constexpr float kVoltageCalibrationGain   = 1.01854f;
static constexpr float kVoltageCalibrationOffset = 1.73636f;

// ESP8266 (D1 mini) original values, kept for reference:
// static constexpr float kAdcVoltageFullScaleV = 51.86f;
// static constexpr float kAdcResolution       = 1024.0f;
// static constexpr float kVoltageCalibrationGain = 0.980;

void VoltageMonitor::begin(uint8_t adcPin, CurrentControl &currentControl) {
    _adcPin = adcPin;
    _currentControl = &currentControl;
    pinMode(_adcPin, INPUT);
    // ESP32: use the widest input range (11 dB) for the ~50 V divider.
    analogSetPinAttenuation(_adcPin, ADC_11db);
}

void VoltageMonitor::update() {
    // Take 3 rapid readings and use the median to reject outliers.
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
    const float calibratedVoltageV = dividerVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;

    // If voltage drops below, or was previously below, the reset threshold,
    // start the EMA again from the current measurement.
    if (calibratedVoltageV < kEmaResetVoltageV || _voltageV < kEmaResetVoltageV) {
        _voltageV = calibratedVoltageV;
    } else {
        _voltageV += kEmaAlpha * (calibratedVoltageV - _voltageV);
    }

    // Overvoltage confirmation: latch a shutdown only after several
    // consecutive over-limit readings. A transient spike must not permanently shut down the charger.
    const bool overVoltage = _voltageV >= kShutdownVoltageV;
    _overvoltageCount = overVoltage ? _overvoltageCount + 1 : 0;
    if (_overvoltageCount >= kOvervoltageRequiredSamples) {
        _currentControl->requestShutdown("overvoltage");
    }
}

float VoltageMonitor::getVoltage() const {
    return _voltageV;
}

int VoltageMonitor::getRawReading() const {
    return _rawReading;
}