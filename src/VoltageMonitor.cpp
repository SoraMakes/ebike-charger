#include "VoltageMonitor.h"
#include "CurrentControl.h"

static constexpr float kShutdownVoltageV = 42.5f;

// ESP32 (TTGO T-Display) ADC: 12-bit, 0..4095 counts.
// Divider: 219k (top) / 4.86k (bottom) -> ratio = 4.86 / 223.86 = 0.021707
// With ADC_0db (~1.1 V full scale input), this maps ~50.67 V at the
// charger output to the ADC input range.
static constexpr float kAdcVoltageFullScaleV = 50.67f;
static constexpr float kAdcResolution       = 4096.0f;

// Actual readings on gen3: 3535:40.6V
static constexpr float kVoltageCalibrationGain   = .854f;
static constexpr float kVoltageCalibrationOffset = 1.73636f;

// ESP8266 (D1 mini) original values, kept for reference:
// static constexpr float kAdcVoltageFullScaleV = 51.86f;
// static constexpr float kAdcResolution       = 1024.0f;
// static constexpr float kVoltageCalibrationGain = 0.980;

void VoltageMonitor::begin(uint8_t adcPin, CurrentControl &currentControl) {
    _adcPin = adcPin;
    _currentControl = &currentControl;
    pinMode(_adcPin, INPUT);
    // ESP32: no internal voltage divider.
    analogSetPinAttenuation(_adcPin, ADC_0db);
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

    // The gain/offset pair is a straight-line fit taken between 30 V and 41 V.
    // Extrapolating it down to zero is invalid: at raw 0 it reports +1.74 V, so
    // a completely dead output never satisfies the `outputVoltageV <= 0` guard
    // in CurrentControl::update(). Worse, the surviving 1.74 V is then used as
    // the divisor in `targetAmps = limitWatts / outputVoltageV`, which pins the
    // demand at the 3 A ceiling exactly when the rail is down or still ramping.
    // Below the fit's range the only honest answer is zero.
    const float calibratedVoltageV = (dividerVoltageV < kCalibrationValidFromV)
        ? 0.0f
        : dividerVoltageV * kVoltageCalibrationGain + kVoltageCalibrationOffset;

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