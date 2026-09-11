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

    /// Shows the boot counter and reset reason, as early in setup() as possible.
    ///
    /// This is the only diagnostic channel that survives a boot loop. The RTC
    /// event ring is lost when the 5 V rail collapses completely, and serial is
    /// unusable because the board resets before a monitor can attach - but the
    /// ST7789 holds its framebuffer for a few hundred milliseconds after the
    /// ESP dies, which is long enough to read. "BROWNOUT" here means a power
    /// problem; "panic"/"int-wdt"/"task-wdt" means a software crash.
    void showBootBanner(uint32_t bootCount, const char *resetReason);

    /// Refreshes the displayed metrics. Call periodically (e.g. every 500 ms).
    void update(PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage,
                const PowerMonitor &power, const EfficiencyEstimator &efficiency);

private:
    TFT_eSPI _tft = TFT_eSPI();
};
