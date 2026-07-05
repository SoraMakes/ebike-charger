#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_HUSB238.h>

enum class PDMode {
    NO_CHARGER,
    FIVE_VOLT_ONLY,
    NEGOTIATING,
    HIGH_VOLTAGE
};

class PDManager {
public:
    PDManager() = default;

    bool begin(TwoWire &wire);
    PDMode detectMode();
    float getNegotiatedVoltage();
    float getNegotiatedCurrentAmps();
    void logAvailablePDOs();

private:
    Adafruit_HUSB238 _husb;

    bool _waitForAttach(uint32_t timeoutMs);
    bool _waitForPDHandshake(uint32_t timeoutMs);
    HUSB238_PDSelection _findBestHighVoltagePDO(float &outWatts);
    void _requestPDO(HUSB238_PDSelection pdo);

    static float _voltageFromSelection(HUSB238_PDSelection sel);
    static float _voltageFromActiveSetting(HUSB238_VoltageSetting vs);
    static float _ampsFromCurrentSetting(HUSB238_CurrentSetting cur);
};
