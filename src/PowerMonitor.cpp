#include "PowerMonitor.h"
#include "DiagLog.h"
#include <string.h>

// Values per CentyLab's RotoPD Pro examples (schematic: 5 mOhm shunt, 7 A max).
static constexpr float    kShuntMaxCurrentA = 7.0f;
static constexpr float    kShuntResistorOhm = 0.005f;
// 6.5A allows full 5A PD contracts with margin for ripple / inrush without
// tripping the hardware circuit breaker.
static constexpr uint32_t kOverCurrentLimitMa = 6500;

bool PowerMonitor::begin() {
    if (!_ina.begin()) {
        return false;
    }

    _ina.setADCRange(1);
    _ina.setMaxCurrentShunt(kShuntMaxCurrentA, kShuntResistorOhm);
    _ina.setShuntVoltageConversionTime(INA238_150_us);
    _ina.setAverage(INA238_16_SAMPLES);
    _ina.setOverCurrentLimit(kOverCurrentLimitMa);
    // SLOW_ALERT ensures overcurrent compares against the averaged reading
    // rather than instantaneous <150us capacitive inrush spikes.
    _ina.setDiagnoseAlertBit(INA238_DIAG_SLOW_ALERT);
    _ina.setDiagnoseAlertBit(INA238_DIAG_ALERT_LATCH);
    return true;
}

void PowerMonitor::update() {
    _busVoltage = _ina.getBusVoltage();
    _current    = _ina.getAmpere();

    // Reading DIAG_ALERT checks for fault flags AND clears the ALERT latch
    // on the RotoPD Pro's hardware circuit breaker.
    const uint16_t diag = _ina.getDiagnoseAlert();
    const uint16_t faults = diag & (
        (1 << INA238_DIAG_SHUNT_OVER_LIMIT) |
        (1 << INA238_DIAG_SHUNT_UNDER_LIMIT)|
        (1 << INA238_DIAG_BUS_OVER_LIMIT)   |
        (1 << INA238_DIAG_BUS_UNDER_LIMIT)  |
        (1 << INA238_DIAG_TEMP_OVER_LIMIT)   |
        (1 << INA238_DIAG_POWER_OVER_LIMIT)
    );

    if (faults != 0) {
        _alertSticky |= faults;
        DiagLog::event("INA ALERT diag=0x%04X%s%s%s%s", diag,
                       (faults & (1 << INA238_DIAG_SHUNT_OVER_LIMIT)) ? " SHNTOL(OCP)" : "",
                       (faults & (1 << INA238_DIAG_BUS_OVER_LIMIT))   ? " BUSOL(OVP)"  : "",
                       (faults & (1 << INA238_DIAG_BUS_UNDER_LIMIT))  ? " BUSUL(UVP)"  : "",
                       (faults & (1 << INA238_DIAG_TEMP_OVER_LIMIT))  ? " TMPOL(OTP)"  : "");
    }
}

float PowerMonitor::getInputVoltage() const { return _busVoltage; }
float PowerMonitor::getInputCurrent() const { return _current; }

const char *PowerMonitor::getAlertText() const {
    static char text[20];
    text[0] = '\0';
    if (_alertSticky & (1 << INA238_DIAG_SHUNT_OVER_LIMIT)) { strcat(text, "OCP "); }
    if (_alertSticky & (1 << INA238_DIAG_BUS_OVER_LIMIT))   { strcat(text, "OVP "); }
    if (_alertSticky & (1 << INA238_DIAG_BUS_UNDER_LIMIT))  { strcat(text, "UVP "); }
    if (_alertSticky & (1 << INA238_DIAG_TEMP_OVER_LIMIT))  { strcat(text, "OTP "); }
    return text;
}
