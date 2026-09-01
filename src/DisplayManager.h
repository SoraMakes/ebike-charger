#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class PDManager;
class ThermalManager;
class CurrentControl;
class VoltageMonitor;
class PowerMonitor;
class EfficiencyEstimator;

/// Drives the TTGO T-Display ST7789 screen.
class DisplayManager {
public:
    /// Initializes the display and shows the initial screen.
    void begin();

    /// Clears the screen and shows a single status message (e.g. a fatal error).
    /// Used for messages that must be visible even when loop() is blocked.
    void showMessage(const char *msg);

    /// Refreshes the displayed metrics. Call periodically (e.g. every 500 ms).
    void update(PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage,
                const PowerMonitor &power, const EfficiencyEstimator &efficiency);

private:
    TFT_eSPI _tft = TFT_eSPI();
};
