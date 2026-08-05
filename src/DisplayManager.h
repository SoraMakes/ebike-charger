#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class PDManager;
class ThermalManager;
class CurrentControl;
class VoltageMonitor;

/// Drives the TTGO T-Display ST7789 screen.
class DisplayManager {
public:
    /// Initializes the display and shows the initial screen.
    void begin();

    /// Refreshes the displayed metrics. Call periodically (e.g. every 500 ms).
    void update(const PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage);

private:
    TFT_eSPI _tft = TFT_eSPI();
};
