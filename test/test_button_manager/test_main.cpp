#include "ButtonManager.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    testButton1Level = HIGH;
    testButton2Level = HIGH;
}

void test_button1_emits_one_event_for_a_debounced_press() {
    ButtonManager buttons;
    buttons.begin();

    ButtonManager::PressEvents events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);
    TEST_ASSERT_FALSE(events.button2Pressed);

    testButton1Level = LOW;
    testNowMs = 10;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testButton1Level = HIGH;
    testNowMs = 20;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testButton1Level = LOW;
    testNowMs = 30;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testNowMs = 49;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testNowMs = 50;
    events = buttons.poll();
    TEST_ASSERT_TRUE(events.button1Pressed);

    testNowMs = 80;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testButton1Level = HIGH;
    testNowMs = 90;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);

    testNowMs = 130;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button1Pressed);
}

void test_button2_emits_a_debounced_press_event() {
    ButtonManager buttons;
    buttons.begin();

    testButton2Level = LOW;
    testNowMs = 10;
    ButtonManager::PressEvents events = buttons.poll();
    TEST_ASSERT_FALSE(events.button2Pressed);

    testNowMs = 29;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button2Pressed);

    testNowMs = 30;
    events = buttons.poll();
    TEST_ASSERT_TRUE(events.button2Pressed);
    TEST_ASSERT_FALSE(events.button1Pressed);

    testNowMs = 40;
    events = buttons.poll();
    TEST_ASSERT_FALSE(events.button2Pressed);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_button1_emits_one_event_for_a_debounced_press);
    RUN_TEST(test_button2_emits_a_debounced_press_event);
    return UNITY_END();
}
