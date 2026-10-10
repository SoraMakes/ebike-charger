#include "DisplayManager.h"
#include "PDManager.h"
#include "ThermalManager.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"
#include "PowerMonitor.h"

void DisplayManager::begin() {
    _tft.init();
    _tft.setRotation(1);  // landscape
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextSize(2);
}

void DisplayManager::showMessage(std::initializer_list<const char *> lines) {
    _tft.fillScreen(TFT_BLACK);
    int y = 0;
    for (const char *line : lines) {
        _tft.drawString(line, 5, y);
        y += 24;
    }
}

void DisplayManager::update(PDManager &pd, const ThermalManager &thermal,
                            const CurrentControl &current, const VoltageMonitor &voltage,
                            const PowerMonitor &power) {
    const float pdVoltageV       = pd.getVoltage();
    const float pdCurrentA       = pd.getCurrent();
    const float measuredVoltageV = power.getInputVoltage();
    const float measuredCurrentA = power.getInputCurrent();
    const float outputVoltageV   = voltage.getVoltage();
    const float currentLimitA    = current.getCurrentLimitA();
    const float maxPowerW        = outputVoltageV * currentLimitA;
    const float tempC            = thermal.getTemperatureC();
    const float allowedInputPowerW = pdCurrentA * measuredVoltageV;
    const float measuredInputPowerW = measuredVoltageV * measuredCurrentA;
    const float inputUtilizationPct = allowedInputPowerW > 0.0f
        ? measuredInputPowerW / allowedInputPowerW * 100.0f
        : 0.0f;
    const char *pdFaultText      = pd.getProtectionFaultText();

    // PD contract plus input.
    _tft.drawString("I " + String(pdVoltageV, 0) + "V/" + String(pdCurrentA, 1) + "A " +
                     String(measuredVoltageV, 1) + "/" + String(measuredCurrentA, 1), 5, 5);

    // Output volts plus current limit.
    _tft.drawString("Out: " + String(outputVoltageV, 1) + "V " + String(currentLimitA, 2) + "A ", 5, 35);

    // Output power plus input volts.
    _tft.drawString("Max: " + String(maxPowerW, 1) + "W  in " + String(measuredVoltageV, 1) + "V", 5, 65);

    // Temperature plus input-power utilization.
    _tft.drawString("T: " + String(tempC, 0) + "C Util: " + String(inputUtilizationPct, 0) + "%", 5, 95);

    // Shutdown, chip fault, INA alert.
    if (pd.isShutdownLatched()) {
        _tft.drawString("STOP: " + String(pd.getShutdownReason()) + "      ", 5, 118);
    } else if (pd.hasProtectionFault()) {
        _tft.drawString("PD FAULT: " + String(pdFaultText) + "     ", 5, 118);
    } else if (power.hasAlert()) {
        _tft.drawString("INA FAULT: " + String(power.getAlertText()) + "    ", 5, 118);
    } else {
        _tft.drawString("                    ", 5, 118);
    }
}
