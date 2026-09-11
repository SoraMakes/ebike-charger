#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

class CurrentControl;

/// Measures ambient temperature and manages thermal fan and charge limits.
class ThermalManager {
public:
    /// Initializes the BMP280 and fan output.
    /// @return true when the temperature sensor was detected.
    bool begin(TwoWire &wire, uint8_t fanPin, CurrentControl &currentControl);

    /// Updates the temperature state and fan output.
    /// Call periodically, approximately once per second.
    void update();

    /// Returns the most recent temperature in degrees Celsius.
    float getTemperatureC() const;

    /// Returns the thermally permitted output power in watts, from 0 to 100.
    float getOutputPowerLimitW() const;

    /// Returns true when the sensor is unavailable or a reading failed.
    bool hasSensorFault() const;

    /// Returns true after an overtemperature or sensor-fault shutdown.
    bool isShutdownLatched() const;

    /// While a source is attached but no PD contract exists yet, the fan is run
    /// at full speed as a keep-alive load. The fan is the largest controllable
    /// load on the 5 V rail, and drawing extra current stops an impatient
    /// powerbank from auto-shutting-down during the AP33772S's slow startup and
    /// the firmware's re-attach retries. Cleared automatically once a contract
    /// exists or the retries are exhausted (see PDManager::isAttachRetrying).
    void setKeepAlive(bool active);

private:
    static constexpr uint16_t kPwmRange = 1023;
    static constexpr uint32_t kFanPwmFrequencyHz = 16000;

    Adafruit_BMP280 _sensor;
    uint8_t _fanPin = 0;
    float _temperatureC = 25.0f;
    float _outputPowerLimitW = 0.0f;
    bool _sensorFault = true;
    bool _shutdownLatched = false;
    bool _keepAlive = false;
    CurrentControl *_currentControl = nullptr;

    void applyFanPwm(uint16_t duty) const;
    void enterShutdown(const char *reason);
};
