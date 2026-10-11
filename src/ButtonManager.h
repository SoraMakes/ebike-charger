#pragma once

#include <Arduino.h>

/// Read the onboard buttons and report debounced press events.
class ButtonManager {
public:
    struct PressEvents {
        bool button1Pressed;
        bool button2Pressed;
    };

    /// Configure both onboard buttons and capture their initial states.
    void begin();

    /// Return press events detected since the previous poll.
    PressEvents poll();

private:
    struct ButtonState {
        bool readingPressed = false;
        bool stablePressed = false;
        uint32_t readingChangedAt = 0;
    };

    static void initializeButton(ButtonState &button, uint8_t pin, uint32_t now);
    static bool updateButton(ButtonState &button, uint8_t pin, uint32_t now);

    ButtonState _button1;
    ButtonState _button2;
};
