#include "PowerMonitor.h"

// Values per CentyLab's RotoPD Pro examples (schematic: 5 mOhm shunt, 7 A max).
static constexpr float kShuntMaxCurrentA = 7.0f;
static constexpr float kShuntResistorOhm = 0.005f;

bool PowerMonitor::begin() {
    if (!_ina.begin()) {
        return false;
    }

    _ina.setADCRange(1);
    _ina.setMaxCurrentShunt(kShuntMaxCurrentA, kShuntResistorOhm);
    _ina.setShuntVoltageConversionTime(INA238_150_us);
    _ina.setAverage(INA238_16_SAMPLES);
    _ina.setOverCurrentLimit(5000);
    _ina.setDiagnoseAlertBit(INA238_DIAG_ALERT_LATCH);
    return true;
}

void PowerMonitor::update() {
    _busVoltage = _ina.getBusVoltage();
    _current    = _ina.getAmpere();
}

float PowerMonitor::getInputVoltage() const { return _busVoltage; }
float PowerMonitor::getInputCurrent() const { return _current; }
