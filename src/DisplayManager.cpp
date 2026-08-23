#include "DisplayManager.h"
#include "PDManager.h"
#include "ThermalManager.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"

void DisplayManager::begin() {
    _tft.init();
    _tft.setRotation(1);  // landscape
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(TFT_GREEN, TFT_BLACK);
    _tft.setTextSize(2);
}

void DisplayManager::update(const PDManager &pd, const ThermalManager &thermal,
                            const CurrentControl &current, const VoltageMonitor &voltage) {
    const float pdVoltageV = pd.getVoltage();
    const float pdCurrentA = pd.getCurrent();
    const float outputVoltageV = voltage.getVoltage();
    const float currentLimitA  = current.getCurrentLimitA();
    const float maxPowerW      = outputVoltageV * currentLimitA;
    const float tempC          = thermal.getTemperatureC();

    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextSize(2);

    // Negotiated PD profile.
    _tft.drawString("PD: " + String(pdVoltageV, 0) + "V " + String(pdCurrentA, 2) + "A", 5, 5);

    // Output voltage + current, and max power at current voltage + target current.
    _tft.drawString("Out: " + String(outputVoltageV, 1) + "V " + String(currentLimitA, 2) + "A", 5, 35);
    _tft.drawString("Max: " + String(maxPowerW, 1) + "W", 5, 65);

    // Temperature.
    _tft.drawString("Temp: " + String(tempC, 1) + " C", 5, 95);

    if (current.isShutdownLatched()) {
        _tft.drawString("STOP: " + String(current.getShutdownReason()), 5, 120);
    } else {
        _tft.drawString("                    ", 5, 120);
    }
}
