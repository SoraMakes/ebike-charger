#include "ThermalManager.h"
#include "PDManager.h"
#include "DiagLog.h"
#include "ThermalPowerDerating.h"

static constexpr float kFanMinimumTemperatureC = 32.0f;
static constexpr float kFanFullTemperatureC = 38.0f;
static constexpr float kSensorMinimumTemperatureC = -40.0f;
static constexpr float kSensorMaximumTemperatureC = 85.0f;
static constexpr uint8_t kReadErrorsBeforeShutdown = 2;
static constexpr uint16_t kPwmRange = 1023;
static constexpr uint32_t kFanPwmFrequencyHz = 16000;
// Minimum duty keeps the fan spinning reliably.
static constexpr uint16_t kFanMinimumDuty = static_cast<uint16_t>(kPwmRange * 0.55f);
static constexpr uint16_t kFanFullDuty = kPwmRange;

// Fan PWM channel.
static constexpr uint8_t kFanLedcChannel = 0;

void ThermalManager::begin(uint8_t fanPin, PDManager &pdManager) {
    _pdManager = &pdManager;
    const uint32_t configuredFrequency = ledcSetup(kFanLedcChannel, kFanPwmFrequencyHz, 10);
    if (configuredFrequency == 0) {
        enterShutdown("fan pwm");
        return;
    }
    pinMode(fanPin, OUTPUT);
    // pinMode before attach.
    ledcAttachPin(fanPin, kFanLedcChannel);

    applyFanPwm(0);
    _sensorFault = !_sensor.begin(0x76);
    if (_sensorFault) {
        enterShutdown("Temp failed");
        return;
    }

    _outputPowerLimitFactor = ThermalPowerDerating::limitFactorForTemperatureC(_temperatureC);
}

void ThermalManager::update() {
    if (_shutdownLatched) {
        applyFanPwm(kFanFullDuty);
        return;
    }

    const float temperature = _sensor.readTemperature();
    const bool temperatureValid = isfinite(temperature) && temperature >= kSensorMinimumTemperatureC && temperature <= kSensorMaximumTemperatureC;
    if (!temperatureValid) {
        _sensorFault = true;
        ++_consecutiveTemperatureReadErrors;
        if (_consecutiveTemperatureReadErrors == kReadErrorsBeforeShutdown) {
            DiagLog::event("temp read error %u/%u",
                           static_cast<unsigned>(_consecutiveTemperatureReadErrors),
                           static_cast<unsigned>(kReadErrorsBeforeShutdown));
        }
        if (_consecutiveTemperatureReadErrors >= kReadErrorsBeforeShutdown) {
            enterShutdown("Temp invalid");
        }
        return;
    }

    _sensorFault = false;
    _consecutiveTemperatureReadErrors = 0;
    _temperatureC = temperature;

    if (_temperatureC >= ThermalPowerDerating::kShutdownTemperatureC) {
        enterShutdown("overtemperature");
        return;
    }

    applyFanPwm(_fanEnabled ? calculateFanDuty(_temperatureC) : 0);
    _outputPowerLimitFactor = ThermalPowerDerating::limitFactorForTemperatureC(_temperatureC);
}

void ThermalManager::enableFan() {
    if (_shutdownLatched || _fanEnabled) {
        return;
    }

    _fanEnabled = true;
    applyFanPwm(calculateFanDuty(_temperatureC));
}

uint16_t ThermalManager::calculateFanDuty(float temperatureC) const {
    if (temperatureC >= kFanFullTemperatureC) {
        return kFanFullDuty;
    }
    if (temperatureC <= kFanMinimumTemperatureC) {
        return kFanMinimumDuty;
    }

    const float temperatureRatio = (temperatureC - kFanMinimumTemperatureC) /
        (kFanFullTemperatureC - kFanMinimumTemperatureC);
    return static_cast<uint16_t>(kFanMinimumDuty + temperatureRatio * (kFanFullDuty - kFanMinimumDuty));
}

float ThermalManager::getTemperatureC() const {
    return _temperatureC;
}

float ThermalManager::getOutputPowerLimitFactor() const {
    return _outputPowerLimitFactor;
}

bool ThermalManager::hasSensorFault() const {
    return _sensorFault;
}

void ThermalManager::applyFanPwm(uint16_t duty) const {
    ledcWrite(kFanLedcChannel, duty);
}

void ThermalManager::enterShutdown(const char *reason) {
    if (!_shutdownLatched) {
        DiagLog::event("safety %s", reason);
    }
    _shutdownLatched = true;
    _outputPowerLimitFactor = 0.0f;
    applyFanPwm(kFanFullDuty);
    _pdManager->shutdown(reason);
}
