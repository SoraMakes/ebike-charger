#include "mocks/Arduino.h"
#include "test_support.h"
#include "PDManager.h"

uint32_t testNowMs = 0;
int testButton1Level = HIGH;
int testButton2Level = HIGH;
int testAdcReading = 0;
FakeSerial Serial;

uint32_t millis() {
    return testNowMs;
}

void pinMode(uint8_t, uint8_t) {}

uint32_t ledcSetup(uint8_t, uint32_t frequency, uint8_t) {
    return frequency;
}

void ledcAttachPin(uint8_t, uint8_t) {}

void ledcWrite(uint8_t, uint32_t) {}

int digitalRead(uint8_t pin) {
    if (pin == 0) {
        return testButton1Level;
    }
    if (pin == 35) {
        return testButton2Level;
    }
    return HIGH;
}

void analogReadResolution(uint8_t) {}

int analogRead(uint8_t) {
    return testAdcReading;
}

void analogSetPinAttenuation(uint8_t, uint8_t) {}

bool ledcAttach(uint8_t, uint32_t, uint8_t) {
    return true;
}

void PDManager::shutdown(const char *) {
    _shutdownLatched = true;
}

void clearTestSerialOutput() {
    Serial.clear();
}

const std::string &getTestSerialOutput() {
    return Serial.output;
}
