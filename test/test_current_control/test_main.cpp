#include "CurrentControl.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    testOutputVoltageV = 10.0f;
}

void test_digipot_does_not_step_before_50_ms() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    CurrentControl control;
    control.begin(wire, voltage);
    control.update(100.0f, false);

    testNowMs = 49;
    control.update(100.0f, false);

    TEST_ASSERT_EQUAL_INT16(0, control.getDigipotStep());
}

void test_digipot_increases_one_step_per_interval() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    CurrentControl control;
    control.begin(wire, voltage);

    testNowMs = 50;
    control.update(100.0f, false);
    TEST_ASSERT_EQUAL_INT16(1, control.getDigipotStep());

    testNowMs = 100;
    control.update(100.0f, false);
    TEST_ASSERT_EQUAL_INT16(2, control.getDigipotStep());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_digipot_does_not_step_before_50_ms);
    RUN_TEST(test_digipot_increases_one_step_per_interval);
    return UNITY_END();
}
