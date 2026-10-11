#include "ButtonManager.h"

namespace {
static constexpr uint8_t kButton1Pin = 0;  // T-Display Button 1.
static constexpr uint8_t kButton2Pin = 35; // T-Display Button 2.
static constexpr uint32_t kButtonDebounceMs = 40;
}  // namespace

void ButtonManager::begin() {
    pinMode(kButton1Pin, INPUT_PULLUP);
    pinMode(kButton2Pin, INPUT);  // GPIO35 has no internal pull-up; the board provides one.

    const uint32_t now = millis();
    initializeButton(_button1, kButton1Pin, now);
    initializeButton(_button2, kButton2Pin, now);
}

ButtonManager::PressEvents ButtonManager::poll() {
    const uint32_t now = millis();
    PressEvents events = {false, false};
    events.button1Pressed = updateButton(_button1, kButton1Pin, now);
    events.button2Pressed = updateButton(_button2, kButton2Pin, now);
    return events;
}

void ButtonManager::initializeButton(ButtonState &button, uint8_t pin, uint32_t now) {
    button.readingPressed = digitalRead(pin) == LOW;
    button.stablePressed = button.readingPressed;
    button.readingChangedAt = now;
}

bool ButtonManager::updateButton(ButtonState &button, uint8_t pin, uint32_t now) {
    const bool pressed = digitalRead(pin) == LOW;
    if (pressed != button.readingPressed) {
        button.readingPressed = pressed;
        button.readingChangedAt = now;
        return false;
    }

    if (button.readingPressed == button.stablePressed ||
        now - button.readingChangedAt < kButtonDebounceMs) {
        return false;
    }

    button.stablePressed = button.readingPressed;
    return button.stablePressed;
}
