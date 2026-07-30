#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_HUSB238.h>

class PDManager {
public:
    PDManager() = default;

    bool begin(TwoWire &wire);

    float getVoltage();
    float getCurrent();
    float getWattage();

private:
    Adafruit_HUSB238 _husb;

    static float _voltageFromActiveSetting(HUSB238_VoltageSetting vs);
    static float _ampsFromCurrentSetting(HUSB238_CurrentSetting cur);
};
