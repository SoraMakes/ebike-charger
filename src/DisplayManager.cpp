#include "DisplayManager.h"
#include "PDManager.h"
#include "ThermalManager.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"
#include "PowerMonitor.h"
#include "DiagLog.h"

#include <stdio.h>

namespace {
static constexpr uint8_t kVisibleEventRows = 15;
static constexpr uint8_t kEventLineSpacing = 9;
// GLCD text is 6 pixels wide; 39 characters fit within the 240-pixel display.
static constexpr uint8_t kMaxEventLineChars = 39;
}  // namespace

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
    _tft.setTextSize(2);
    int y = 0;
    for (const char *line : lines) {
        _tft.drawString(line, 5, y);
        y += 24;
    }
    _screenDirty = true;
}

void DisplayManager::toggleDisplayMode() {
    _showEventLog = !_showEventLog;
    _screenDirty = true;
}

void DisplayManager::updateEventLog() {
    const uint32_t eventSequence = DiagLog::eventSequence();
    const uint32_t uptimeSeconds = millis() / 1000;
    if (!_screenDirty && eventSequence == _shownEventSequence && uptimeSeconds == _shownUptimeSeconds) {
        return;
    }

    _tft.fillScreen(TFT_BLACK);
    _tft.setTextSize(1);

    const uint8_t retainedEntryCount = DiagLog::retainedEntryCount();
    const uint8_t firstVisibleEntry = retainedEntryCount > kVisibleEventRows
        ? retainedEntryCount - kVisibleEventRows
        : 0;
    int y = 0;
    for (uint8_t index = firstVisibleEntry; index < retainedEntryCount; ++index) {
        DiagLog::Entry entry;
        if (!DiagLog::getEntry(index, entry)) {
            continue;
        }

        char line[kMaxEventLineChars + 1];
        snprintf(line, sizeof(line), "%lu.%01lu %s",
                 static_cast<unsigned long>(entry.timestampMs / 1000),
                 static_cast<unsigned long>((entry.timestampMs % 1000) / 100), entry.text);
        _tft.drawString(line, 2, y);
        y += kEventLineSpacing;
    }

    char uptime[16];
    snprintf(uptime, sizeof(uptime), " |%lu", static_cast<unsigned long>(uptimeSeconds));
    _tft.setTextDatum(TR_DATUM);
    _tft.drawString(uptime, 238, 0);
    _tft.setTextDatum(TL_DATUM);

    _shownEventSequence = eventSequence;
    _shownUptimeSeconds = uptimeSeconds;
    _screenDirty = false;
}

void DisplayManager::update(PDManager &pd, const ThermalManager &thermal,
                            const CurrentControl &current, const VoltageMonitor &voltage,
                            const PowerMonitor &power, bool limitedCurrentMode) {
    if (_showEventLog) {
        updateEventLog();
        return;
    }

    if (_screenDirty) {
        _tft.fillScreen(TFT_BLACK);
        _tft.setTextSize(2);
        _screenDirty = false;
    }

    updateStatus(pd, thermal, current, voltage, power, limitedCurrentMode);
}

void DisplayManager::updateStatus(PDManager &pd, const ThermalManager &thermal,
                                  const CurrentControl &current, const VoltageMonitor &voltage,
                                  const PowerMonitor &power, bool limitedCurrentMode) {
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

    // Show the PD contract and measured input utilization.
    _tft.drawString("I: " + String(pdVoltageV, 0) + "V/" + String(pdCurrentA, 1) + "A | " + String(inputUtilizationPct, 0) + "%  ", 5, 5);

    // Put measured input voltage and current below the contract line.
    _tft.setTextSize(1);
    _tft.drawString("Actual: " + String(measuredVoltageV, 1) + "V " + String(measuredCurrentA, 1) + "A " + String(measuredInputPowerW, 0) + "W  ", 5, 23);
    _tft.setTextSize(2);

    // Show output voltage, estimated current, and power.
    _tft.drawString("O: " + String(outputVoltageV, 1) + "V " + String(currentLimitA, 2) + "A " + String(maxPowerW, 0) + "W  ", 5, 42);

    _tft.drawString("T: " + String(tempC, 0) + "C  ", 5, 75);
    _tft.drawString(limitedCurrentMode ? "LIMIT: 2A  " : "              ", 5, 95);

    updateFaultStatus(pd, power);
}

void DisplayManager::updateFaultStatus(const PDManager &pd, const PowerMonitor &power) {
    const char *pdFaultText = pd.getProtectionFaultText();

    // Show the most important active fault.
    if (pd.isShutdownLatched()) {
        _tft.drawString("STOP: " + String(pd.getShutdownReason()) + "  ", 5, 118);
    } else if (pd.hasProtectionFault()) {
        _tft.drawString("PD FAULT: " + String(pdFaultText) + "  ", 5, 118);
    } else if (power.hasAlert()) {
        _tft.drawString("INA FAULT: " + String(power.getAlertText()) + "  ", 5, 118);
    } else {
        _tft.drawString("                    ", 5, 118);
    }
}
