#include "mocks/Arduino.h"
#include "test_support.h"
#include "VoltageMonitor.h"

uint32_t testNowMs = 0;
float testOutputVoltageV = 10.0f;
FakeSerial Serial;

uint32_t millis() {
    return testNowMs;
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
