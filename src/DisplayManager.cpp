#include "DisplayManager.h"
#include "PDManager.h"
#include "ThermalManager.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"
#include "PowerMonitor.h"
#include "EfficiencyEstimator.h"

void DisplayManager::begin() {
    _tft.init();
    _tft.setRotation(1);  // landscape
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(TFT_GREEN, TFT_BLACK);
    _tft.setTextSize(2);
}

void DisplayManager::showMessage(const char *msg) {
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextSize(2);
    // TFT_eSPI drawString ignores '\n', split manually.
    int y = 5;
    const char *lineStart = msg;
    for (const char *p = msg;; ++p) {
        if (*p == '\n' || *p == '\0') {
            const int len = static_cast<int>(p - lineStart);
            if (len > 0) {
                _tft.drawString(String(lineStart, len), 5, y);
                y += 24;
            }
            if (*p == '\0') {
                break;
            }
            lineStart = p + 1;
        }
    }
}

void DisplayManager::update(PDManager &pd, const ThermalManager &thermal,
                            const CurrentControl &current, const VoltageMonitor &voltage,
                            const PowerMonitor &power, const EfficiencyEstimator &efficiency) {
    const float pdVoltageV = pd.getVoltage();
    const float pdCurrentA = pd.getCurrent();
    const float measuredVoltageV = power.getInputVoltage();
    const float measuredCurrentA = power.getInputCurrent();
    const float outputVoltageV = voltage.getVoltage();
    const float currentLimitA  = current.getCurrentLimitA();
    const float maxPowerW      = outputVoltageV * currentLimitA;
    const float tempC          = thermal.getTemperatureC();
    const float efficiencyPct  = efficiency.getEfficiency() * 100.0f;

    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextSize(2);

    // Line 1: negotiated PD contract plus measured PD input (INA238).
    _tft.drawString("I " + String(pdVoltageV, 0) + "V/" + String(pdCurrentA, 1) + "A " +
                     String(measuredVoltageV, 1) + "/" + String(measuredCurrentA, 1), 5, 5);

    // Line 2: charger output voltage plus active current limit.
    _tft.drawString("Out: " + String(outputVoltageV, 1) + "V " + String(currentLimitA, 2) + "A ", 5, 35);

    // Line 3: output power at current limit plus measured PD input volts.
    _tft.drawString("Max: " + String(maxPowerW, 1) + "W  in " +
                     String(measuredVoltageV, 1) + "V", 5, 65);

    // Temperature and VR efficiency estimate on one line.
    _tft.drawString("Temp: " + String(tempC, 1) + "C " + String(efficiencyPct, 0) + "%", 5, 95);

    // Bottom line: shutdown, chip fault, INA alert.
    if (current.isShutdownLatched()) {
        _tft.drawString("STOP: " + String(current.getShutdownReason()) + "      ", 5, 118);
    } else if (pd.hasChipFault()) {
        _tft.drawString("PD FAULT: " + String(pd.getFaultText()) + "     ", 5, 118);
    } else if (power.hasAlert()) {
        _tft.drawString("INA FAULT: " + String(power.getAlertText()) + "    ", 5, 118);
    } else {
        _tft.drawString("                    ", 5, 118);
    }
}
