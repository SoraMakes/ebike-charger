#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <INA238.h>

/// Input voltage/current via the onboard INA238.
class PowerMonitor {
public:
    bool  begin();
    void  update();
    float getInputVoltage() const;
    float getInputCurrent() const;
    bool hasAlert() const { return _alertSticky != 0; }

    // Latched faults as text.
    const char *getAlertText() const;

private:
    INA238 _ina{0x40};
    float _busVoltage = 0.0f;
    float _current    = 0.0f;
    uint16_t _alertSticky = 0;
};
