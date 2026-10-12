#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <math.h>
#include <string>

uint32_t millis();

static constexpr uint8_t INPUT = 0x01;
static constexpr uint8_t INPUT_PULLUP = 0x02;
static constexpr uint8_t OUTPUT = 0x03;
static constexpr int LOW = 0;
static constexpr int HIGH = 1;
static constexpr uint8_t ADC_0db = 0;

void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void analogReadResolution(uint8_t bits);
int analogRead(uint8_t pin);
void analogSetPinAttenuation(uint8_t pin, uint8_t attenuation);
bool ledcAttach(uint8_t pin, uint32_t frequency, uint8_t resolutionBits);
uint32_t ledcSetup(uint8_t channel, uint32_t frequency, uint8_t resolutionBits);
void ledcAttachPin(uint8_t pin, uint8_t channel);
void ledcWrite(uint8_t channel, uint32_t duty);

class FakeSerial {
public:
    int printf(const char *format, ...) {
        char text[128];
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        if (length > 0) {
            const size_t written = static_cast<size_t>(length) < sizeof(text)
                ? static_cast<size_t>(length)
                : sizeof(text) - 1;
            output.append(text, written);
        }
        return length;
    }

    void clear() { output.clear(); }

    std::string output;
};

extern FakeSerial Serial;

template <typename T>
constexpr T min(T left, T right) {
    return left < right ? left : right;
}

template <typename T>
constexpr T max(T left, T right) {
    return left > right ? left : right;
}

template <typename T>
constexpr T constrain(T value, T low, T high) {
    return min(max(value, low), high);
}
