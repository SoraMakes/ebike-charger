#pragma once

#include <cstdint>

class Adafruit_BMP280 {
public:
    struct State {
        bool beginSucceeds;
        float temperatureC;
    };

    static State &state() {
        static State sensorState = {true, 25.0f};
        return sensorState;
    }

    static void reset() {
        state() = {true, 25.0f};
    }

    bool begin(uint8_t) {
        return state().beginSucceeds;
    }

    float readTemperature() {
        return state().temperatureC;
    }
};
