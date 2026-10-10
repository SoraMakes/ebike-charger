#pragma once

#include <Arduino.h>
#include <initializer_list>
#include <TFT_eSPI.h>

class PDManager;
class ThermalManager;
class CurrentControl;
class VoltageMonitor;
class PowerMonitor;

/// TTGO T-Display ST7789 driver.
class DisplayManager {
public:
    void begin();

    /// Status message, visible when blocked.
    void showMessage(std::initializer_list<const char *> lines);

    void update(PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage,
                const PowerMonitor &power);

private:
    TFT_eSPI _tft = TFT_eSPI();
};
