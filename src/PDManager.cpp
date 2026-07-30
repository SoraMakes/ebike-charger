#include "PDManager.h"

bool PDManager::begin(TwoWire &wire) {
    return _husb.begin(HUSB238_I2CADDR_DEFAULT, &wire);
}

float PDManager::getVoltage() {
    return _voltageFromActiveSetting(_husb.getPDSrcVoltage());
}

float PDManager::getCurrent() {
    return _ampsFromCurrentSetting(_husb.getPDSrcCurrent());
}

float PDManager::getWattage() {
    return getVoltage() * getCurrent();
}

float PDManager::_voltageFromActiveSetting(HUSB238_VoltageSetting vs) {
    switch (vs) {
        case PD_5V:  return 5.0f;
        case PD_9V:  return 9.0f;
        case PD_12V: return 12.0f;
        case PD_15V: return 15.0f;
        case PD_18V: return 18.0f;
        case PD_20V: return 20.0f;
        default:     return 0.0f;
    }
}

float PDManager::_ampsFromCurrentSetting(HUSB238_CurrentSetting cur) {
    switch (cur) {
        case CURRENT_0_5_A:  return 0.5f;
        case CURRENT_0_7_A:  return 0.7f;
        case CURRENT_1_0_A:  return 1.0f;
        case CURRENT_1_25_A: return 1.25f;
        case CURRENT_1_5_A:  return 1.5f;
        case CURRENT_1_75_A: return 1.75f;
        case CURRENT_2_0_A:  return 2.0f;
        case CURRENT_2_25_A: return 2.25f;
        case CURRENT_2_50_A: return 2.5f;
        case CURRENT_2_75_A: return 2.75f;
        case CURRENT_3_0_A:  return 3.0f;
        case CURRENT_3_25_A: return 3.25f;
        case CURRENT_3_5_A:  return 3.5f;
        case CURRENT_4_0_A:  return 4.0f;
        case CURRENT_4_5_A:  return 4.5f;
        case CURRENT_5_0_A:  return 5.0f;
        default:             return 0.0f;
    }
}
