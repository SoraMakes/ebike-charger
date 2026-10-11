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

    /// Switch between the status and event-log views.
    void toggleDisplayMode();

    void update(PDManager &pd, const ThermalManager &thermal,
                const CurrentControl &current, const VoltageMonitor &voltage,
                const PowerMonitor &power, bool limitedCurrentMode);

private:
    TFT_eSPI _tft = TFT_eSPI();
    bool _showEventLog = false;
    bool _screenDirty = true;
    uint32_t _shownEventSequence = 0;

    void updateStatus(PDManager &pd, const ThermalManager &thermal,
                      const CurrentControl &current, const VoltageMonitor &voltage,
                      const PowerMonitor &power, bool limitedCurrentMode);
    void updateEventLog();
    void updateFaultStatus(const PDManager &pd, const PowerMonitor &power);
};
