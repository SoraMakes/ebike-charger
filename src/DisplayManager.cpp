#include "DisplayManager.h"
#include "PDManager.h"
#include "ThermalManager.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"
#include "PowerMonitor.h"
#include "EfficiencyEstimator.h"
#include "DiagLog.h"

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
    // TFT_eSPI's drawString does not interpret '\n', so split into lines.
    int y = 5;
    const char *lineStart = msg;
    for (const char *p = msg;; ++p) {
        if (*p == '\n' || *p == '\0') {
            const int len = static_cast<int>(p - lineStart);
            if (len > 0) {
                _tft.drawString(String(lineStart, len), 5, y);
                y += 24;  // 2x font line height
            }
            if (*p == '\0') {
                break;
            }
            lineStart = p + 1;
        }
    }
}

void DisplayManager::showBootBanner(uint32_t bootCount, const char *resetReason) {
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextSize(3);
    _tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft.drawString("boot #" + String(bootCount), 5, 20);
    _tft.setTextColor(TFT_RED, TFT_BLACK);
    _tft.drawString(String(resetReason), 5, 60);
    _tft.setTextSize(2);
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

    // Negotiated PD profile with measured input voltage/current in braces.
    // Current shown with 1 decimal: a 1.5A PDO must not round up to "2A".
    _tft.drawString("I " + String(pdVoltageV, 0) + "V/" + String(pdCurrentA, 1) + "A " +
                     String(measuredVoltageV, 1) + "/" + String(measuredCurrentA, 1), 5, 5);

    // Output voltage + current, and max power at current voltage + target current.
    _tft.drawString("Out: " + String(outputVoltageV, 1) + "V " + String(currentLimitA, 2) + "A ", 5, 35);

    // The measured AP33772S VOUT voltage - the regulator's INPUT. Shown as a
    // number, not a boolean: the old "(e)/(d)" flag only tested VOUT >= 1V, so
    // a contract that collapsed from 20V back to 5V still read as "enabled".
    // Comparing this against the contract voltage on the line above is what
    // reveals a dead contract.
    _tft.drawString("Max: " + String(maxPowerW, 1) + "W  in " +
                     String(pd.getVoutVoltageV(), 1) + "V", 5, 65);

    // Temperature and VR efficiency estimate on one line.
    _tft.drawString("Temp: " + String(tempC, 1) + "C " + String(efficiencyPct, 0) + "%", 5, 95);

    // Bottom line, in priority order:
    //  1. an app-triggered shutdown (overvoltage / overtemperature),
    //  2. any AP33772S protection that has tripped since boot, or
    //  3. any INA238 hardware alert (circuit breaker trip) since boot.
    if (current.isShutdownLatched()) {
        _tft.drawString("STOP: " + String(current.getShutdownReason()) + "      ", 5, 118);
    } else if (pd.hasChipFault()) {
        _tft.drawString("PD FAULT: " + String(pd.getFaultText()) + "     ", 5, 118);
    } else if (power.hasAlert()) {
        _tft.drawString("INA FAULT: " + String(power.getAlertText()) + "    ", 5, 118);
    } else {
        // With no fault to report, keep the boot counter and reset reason
        // visible. On a board that reboots faster than a serial monitor can
        // attach, this line is the only place the reason is ever readable.
        _tft.drawString("b" + String(DiagLog::bootCount()) + " " +
                        String(DiagLog::resetReasonText()) + "          ", 5, 118);
    }

    // Power-on timer, e.g. "12.3s". Drawn right-aligned on the bottom line so
    // it is always visible, even when a fault message occupies the left side.
    // The ST7789 holds its framebuffer for a few hundred ms after the ESP
    // dies, so this is what tells you exactly when it crashed. Refreshed by
    // the 100 ms display task.
    char uptime[16];
    snprintf(uptime, sizeof(uptime), "%.1fs", millis() / 1000.0f);
    _tft.setTextDatum(TR_DATUM);
    _tft.drawString(String(uptime), 235, 118);
    _tft.setTextDatum(TL_DATUM);
}
