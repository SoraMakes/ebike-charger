#include "mocks/Arduino.h"
#include "test_support.h"
#include "VoltageMonitor.h"

uint32_t testNowMs = 0;
float testOutputVoltageV = 10.0f;
int testButton1Level = HIGH;
int testButton2Level = HIGH;
FakeSerial Serial;

uint32_t millis() {
    return testNowMs;
}

void pinMode(uint8_t, uint8_t) {}

int digitalRead(uint8_t pin) {
    if (pin == 0) {
        return testButton1Level;
    }
    if (pin == 35) {
        return testButton2Level;
    }
    return HIGH;
}

float VoltageMonitor::getVoltage() const {
    return testOutputVoltageV;
}

void clearTestSerialOutput() {
    Serial.clear();
}

const std::string &getTestSerialOutput() {
    return Serial.output;
}
