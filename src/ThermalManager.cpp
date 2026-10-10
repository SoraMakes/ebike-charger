#include "ThermalManager.h"
#include "PDManager.h"
#include "DiagLog.h"

static constexpr float kFanMinimumTemperatureC = 32.0f;
static constexpr float kFanFullTemperatureC = 38.0f;
static constexpr float kDeratingTemperatureC = 40.0f;
static constexpr float kShutdownTemperatureC = 50.0f;
static constexpr float kSensorMinimumTemperatureC = -40.0f;
static constexpr float kSensorMaximumTemperatureC = 85.0f;
static constexpr float kMaximumThermalPowerW = 100.0f;
static constexpr uint16_t kPwmRange = 1023;
static constexpr uint32_t kFanPwmFrequencyHz = 16000;
// Minimum duty keeps the fan spinning reliably.
static constexpr uint16_t kFanMinimumDuty = static_cast<uint16_t>(kPwmRange * 0.55f);
static constexpr uint16_t kFanFullDuty = kPwmRange;

// Fan PWM channel.
static constexpr uint8_t kFanLedcChannel = 0;

bool ThermalManager::begin(uint8_t fanPin, PDManager &pdManager) {
    _pdManager = &pdManager;
    const uint32_t configuredFrequency = ledcSetup(kFanLedcChannel, kFanPwmFrequencyHz, 10);
    if (configuredFrequency == 0) {
        DiagLog::event("safety fan pwm");
    }
    pinMode(fanPin, OUTPUT);
    // pinMode before attach.
    ledcAttachPin(fanPin, kFanLedcChannel);

    applyFanPwm(kFanMinimumDuty);
    _sensorFault = !_sensor.begin(0x76);
    if (_sensorFault) {
        enterShutdown("Temp failed");
        return false;
    }

    _outputPowerLimitW = kMaximumThermalPowerW;
    return true;
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
        enterShutdown("Temp invalid");
        return;
    }

    _sensorFault = false;
    _temperatureC = temperature;

    if (_temperatureC >= kShutdownTemperatureC) {
        enterShutdown("overtemperature");
        return;
    }

    applyFanPwm(calculateFanDuty(_temperatureC));
    _outputPowerLimitW = calculateOutputPowerLimit(_temperatureC);
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

float ThermalManager::calculateOutputPowerLimit(float temperatureC) const {
    if (temperatureC <= kDeratingTemperatureC) {
        return kMaximumThermalPowerW;
    }

    const float deratingRatio = (temperatureC - kDeratingTemperatureC) /
        (kShutdownTemperatureC - kDeratingTemperatureC);
    return kMaximumThermalPowerW * (1.0f - deratingRatio);
}

float ThermalManager::getTemperatureC() const {
    return _temperatureC;
}

float ThermalManager::getOutputPowerLimitW() const {
    return _outputPowerLimitW;
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
    _outputPowerLimitW = 0.0f;
    applyFanPwm(kFanFullDuty);
    _pdManager->shutdown(reason);
}
