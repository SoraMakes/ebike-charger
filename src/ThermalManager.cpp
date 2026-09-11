#include "ThermalManager.h"
#include "CurrentControl.h"

static constexpr float kFanMinimumTemperatureC = 32.0f;
static constexpr float kFanFullTemperatureC = 38.0f;
static constexpr float kDeratingTemperatureC = 40.0f;
static constexpr float kShutdownTemperatureC = 50.0f;
static constexpr float kMaximumThermalPowerW = 100.0f;
static constexpr uint16_t kPwmRange = 1023;
static constexpr uint32_t kFanPwmFrequencyHz = 16000;
static constexpr uint16_t kFanMinimumDuty = 563;  // approximately 55% of 1023
static constexpr uint16_t kFanFullDuty = kPwmRange;

// ESP32 LEDC channel used for the fan PWM.
static constexpr uint8_t kFanLedcChannel = 0;

bool ThermalManager::begin(TwoWire &wire, uint8_t fanPin, CurrentControl &currentControl) {
    _fanPin = fanPin;
    _currentControl = &currentControl;
    // ESP32: configure the LEDC PWM peripheral for the fan.
    const uint32_t configuredFrequency = ledcSetup(kFanLedcChannel, kFanPwmFrequencyHz, 10);
    if (configuredFrequency == 0) {
        Serial.println("[THERM] Fan PWM setup failed");
    }
    pinMode(_fanPin, OUTPUT);
    // Attach LEDC after pinMode: on ESP32 pinMode configures the GPIO matrix
    // and must not overwrite the peripheral output routing.
    ledcAttachPin(_fanPin, kFanLedcChannel);

    applyFanPwm(kFanMinimumDuty);
    _sensorFault = !_sensor.begin(0x76);
    if (_sensorFault) {
        enterShutdown("Temp failed");
        return false;
    }

    _outputPowerLimitW = kMaximumThermalPowerW;
    applyFanPwm(kFanMinimumDuty);
    return true;
}

void ThermalManager::update() {
    if (_shutdownLatched) {
        applyFanPwm(kFanFullDuty);
        return;
    }

    const float temperature = _sensor.readTemperature();

    if (!isfinite(temperature) ||
        temperature < -40.0f || temperature > 85.0f) {
        _sensorFault = true;
        enterShutdown("Temp invalid");
        return;
    }

    _sensorFault = false;
    _temperatureC = temperature;

    uint16_t fanDuty = kFanMinimumDuty;
    if (_keepAlive) {
        fanDuty = kFanFullDuty;
    } else if (_temperatureC >= kFanFullTemperatureC) {
        fanDuty = kFanFullDuty;
    } else if (_temperatureC > kFanMinimumTemperatureC) {
        const float ratio = (_temperatureC - kFanMinimumTemperatureC) / (kFanFullTemperatureC - kFanMinimumTemperatureC);
        fanDuty = static_cast<uint16_t>(kFanMinimumDuty + ratio * (kFanFullDuty - kFanMinimumDuty));
    }
    applyFanPwm(fanDuty);

    if (_temperatureC >= kShutdownTemperatureC) {
        enterShutdown("overtemperature");
        return;
    }

    if (_temperatureC <= kDeratingTemperatureC) {
        _outputPowerLimitW = kMaximumThermalPowerW;
    } else {
        const float ratio = (_temperatureC - kDeratingTemperatureC) / (kShutdownTemperatureC - kDeratingTemperatureC);
        _outputPowerLimitW = kMaximumThermalPowerW * (1.0f - ratio);
    }
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

bool ThermalManager::isShutdownLatched() const {
    return _shutdownLatched;
}

void ThermalManager::setKeepAlive(bool active) {
    _keepAlive = active;
}

void ThermalManager::applyFanPwm(uint16_t duty) const {
    // ESP32: write via the LEDC peripheral.
    ledcWrite(kFanLedcChannel, duty);
}

void ThermalManager::enterShutdown(const char *reason) {
    if (!_shutdownLatched) {
        Serial.printf("[THERM] Shutdown: %s\n", reason);
    }
    _shutdownLatched = true;
    _outputPowerLimitW = 0.0f;
    applyFanPwm(kFanFullDuty);
    _currentControl->requestShutdown(reason);
}
