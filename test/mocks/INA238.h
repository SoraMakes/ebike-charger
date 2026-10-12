#pragma once

#include <cstdint>

enum {
    INA238_150_us = 2,
    INA238_16_SAMPLES = 2,
    INA238_DIAG_SHUNT_OVER_LIMIT = 0,
    INA238_DIAG_SHUNT_UNDER_LIMIT = 1,
    INA238_DIAG_BUS_OVER_LIMIT = 2,
    INA238_DIAG_BUS_UNDER_LIMIT = 3,
    INA238_DIAG_TEMP_OVER_LIMIT = 4,
    INA238_DIAG_POWER_OVER_LIMIT = 5,
    INA238_DIAG_SLOW_ALERT = 6,
    INA238_DIAG_ALERT_LATCH = 7,
};

namespace FakeINA238 {

enum class Read : uint8_t {
    None,
    Voltage,
    Current,
    Diagnostics,
};

struct State {
    bool connected = true;
    float busVoltage = 20.0f;
    float current = 0.5f;
    uint16_t diagnostics = 0;
    Read failingRead = Read::None;
    uint8_t failuresRemaining = 0;
    int errorCode = -1;
};

inline State &state() {
    static State value;
    return value;
}

inline void reset() {
    state() = State{};
}

}  // namespace FakeINA238

class INA238 {
public:
    explicit INA238(uint8_t) {}

    bool begin() {
        return FakeINA238::state().connected;
    }

    void setADCRange(bool) {}
    int setMaxCurrentShunt(float, float) { return 0; }
    bool setShuntVoltageConversionTime(uint8_t) { return true; }
    bool setTemperatureConversionTime(uint8_t) { return true; }
    bool setAverage(uint8_t) { return true; }
    void setOverCurrentLimit(uint32_t) {}
    void setDiagnoseAlertBit(uint8_t) {}

    float getBusVoltage() {
        return read(FakeINA238::Read::Voltage) ? 0.0f : FakeINA238::state().busVoltage;
    }

    float getAmpere() {
        return read(FakeINA238::Read::Current) ? 0.0f : FakeINA238::state().current;
    }

    uint16_t getDiagnoseAlert() {
        if (read(FakeINA238::Read::Diagnostics)) {
            return 0;
        }
        const uint16_t diagnostics = FakeINA238::state().diagnostics;
        FakeINA238::state().diagnostics = 0;
        return diagnostics;
    }

    int getLastError() {
        const int error = _error;
        _error = 0;
        return error;
    }

private:
    bool read(FakeINA238::Read measurement) {
        _error = 0;
        FakeINA238::State &sensor = FakeINA238::state();
        if (sensor.failingRead != measurement || sensor.failuresRemaining == 0) {
            return false;
        }

        --sensor.failuresRemaining;
        _error = sensor.errorCode;
        return true;
    }

    int _error = 0;
};
