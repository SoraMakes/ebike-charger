#include "ThermalManager.h"

// ESP8266 analogWrite range after analogWriteRange().
static constexpr uint16_t kPwmRange = 1023;
static constexpr uint16_t kFanSafetyDuty = kPwmRange / 2;
static constexpr uint32_t kFanPwmFrequencyHz = 16000;

void ThermalManager::begin(uint8_t fanPin) {
    _fanPin = fanPin;
    analogWriteRange(kPwmRange);
    analogWriteFreq(kFanPwmFrequencyHz);
    pinMode(_fanPin, OUTPUT);
    applyFanPwm();
}

void ThermalManager::update() {
    // Keep this as the periodic control hook for the future temperature
    // policy. The current safety behavior is fixed and independent of load.
    applyFanPwm();
}

void ThermalManager::applyFanPwm() const {
    analogWrite(_fanPin, kFanSafetyDuty);
}
