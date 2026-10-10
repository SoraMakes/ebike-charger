#include "PowerMonitor.h"
#include "DiagLog.h"
#include <string.h>

// INA238 shunt calibration and measurement range.
static constexpr float    kShuntMaxCurrentA = 7.0f;
static constexpr float    kShuntResistorOhm = 0.005f;
// Overcurrent alert includes headroom for inrush.
static constexpr uint32_t kOverCurrentLimitMa = 6500;
static constexpr uint16_t kMonitoredFaultMask =
    (1 << INA238_DIAG_SHUNT_OVER_LIMIT) |
    (1 << INA238_DIAG_SHUNT_UNDER_LIMIT) |
    (1 << INA238_DIAG_BUS_OVER_LIMIT) |
    (1 << INA238_DIAG_BUS_UNDER_LIMIT) |
    (1 << INA238_DIAG_TEMP_OVER_LIMIT) |
    (1 << INA238_DIAG_POWER_OVER_LIMIT);

bool PowerMonitor::begin() {
    if (!_ina.begin()) {
        return false;
    }
    // Use the higher-resolution shunt range for this shunt/current configuration.
    _ina.setADCRange(true);
    _ina.setMaxCurrentShunt(kShuntMaxCurrentA, kShuntResistorOhm);
    _ina.setShuntVoltageConversionTime(INA238_150_us);
    _ina.setAverage(INA238_16_SAMPLES);
    _ina.setOverCurrentLimit(kOverCurrentLimitMa);
    // Configure averaged alerts that remain active until read.
    _ina.setDiagnoseAlertBit(INA238_DIAG_SLOW_ALERT);
    _ina.setDiagnoseAlertBit(INA238_DIAG_ALERT_LATCH);
    return true;
}

void PowerMonitor::update() {
    _busVoltage = _ina.getBusVoltage();
    _current    = _ina.getAmpere();
    // Reading the diagnostic register clears the INA238 alert status.
    const uint16_t diag = _ina.getDiagnoseAlert();
    const uint16_t newlyLatchedFaults = diag & kMonitoredFaultMask & ~_alertSticky;
    if (newlyLatchedFaults == 0) {
        return;
    }
    _alertSticky |= newlyLatchedFaults;
    DiagLog::event("safety ina %s", getAlertText());
}

float PowerMonitor::getInputVoltage() const { return _busVoltage; }
float PowerMonitor::getInputCurrent() const { return _current; }

const char *PowerMonitor::getAlertText() const {
    static char text[33];
    text[0] = '\0';
    if (_alertSticky & (1 << INA238_DIAG_SHUNT_OVER_LIMIT)) { strcat(text, "OCP "); }
    if (_alertSticky & (1 << INA238_DIAG_SHUNT_UNDER_LIMIT)) { strcat(text, "SHUNT-UV "); }
    if (_alertSticky & (1 << INA238_DIAG_BUS_OVER_LIMIT))   { strcat(text, "OVP "); }
    if (_alertSticky & (1 << INA238_DIAG_BUS_UNDER_LIMIT))  { strcat(text, "UVP "); }
    if (_alertSticky & (1 << INA238_DIAG_TEMP_OVER_LIMIT))  { strcat(text, "OTP "); }
    if (_alertSticky & (1 << INA238_DIAG_POWER_OVER_LIMIT)) { strcat(text, "PWR-OV "); }
    return text;
}
