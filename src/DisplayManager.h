#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class PDManager;
class ThermalManager;
class CurrentControl;
class VoltageMonitor;
class PowerMonitor;
class EfficiencyEstimator;

/// TTGO T-Display ST7789 driver.
class DisplayManager {
public:
    void begin();

    /// Status message, visible when blocked.
    void showMessage(const char *msg);

    void update(PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage,
                const PowerMonitor &power, const EfficiencyEstimator &efficiency);

private:
    TFT_eSPI _tft = TFT_eSPI();
};
