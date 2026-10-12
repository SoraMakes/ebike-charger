#include "CurrentControl.h"
#include "PDManager.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    testAdcReading = 632;
}

static void initializeVoltage(VoltageMonitor &voltage, PDManager &pdManager) {
    voltage.begin(1, pdManager);
    voltage.update();
}

void test_digipot_does_not_step_before_75_ms() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);
    control.update(100.0f, false, 4.0f);

    testNowMs = 74;
    control.update(100.0f, false, 4.0f);

    TEST_ASSERT_EQUAL_INT16(0, control.getDigipotStep());

    testNowMs = 75;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(1, control.getDigipotStep());
}

void test_digipot_increases_one_step_per_interval() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);

    testNowMs = 75;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(1, control.getDigipotStep());

    testNowMs = 150;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(2, control.getDigipotStep());
}

void test_final_three_steps_require_500_ms_of_continuous_demand() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);

    for (uint32_t now = 75; now <= 4500; now += 75) {
        testNowMs = now;
        control.update(100.0f, false, 4.0f);
    }
    TEST_ASSERT_EQUAL_INT16(60, control.getDigipotStep());

    testNowMs = 4575;
    control.update(100.0f, false, 4.0f);
    testNowMs = 4800;
    control.update(3.8f * voltage.getVoltage(), false, 4.0f);
    testNowMs = 4801;
    control.update(100.0f, false, 4.0f);
    testNowMs = 5300;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(60, control.getDigipotStep());

    testNowMs = 5301;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(61, control.getDigipotStep());

    testNowMs = 5800;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(61, control.getDigipotStep());
    testNowMs = 5801;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_EQUAL_INT16(62, control.getDigipotStep());
}

void test_output_current_uses_average_calibration() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.13819f, control.getCurrentLimitA());

    testNowMs = 75;
    control.update(100.0f, false, 4.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1987212f, control.getCurrentLimitA());
}

void test_limited_mode_caps_output_current_at_two_amps() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);

    for (uint8_t step = 0; step < 100; ++step) {
        testNowMs += 75;
        control.update(1000.0f, false, 2.0f);
    }

    TEST_ASSERT_TRUE(control.getCurrentLimitA() <= 2.0f);
    TEST_ASSERT_TRUE(control.getCurrentLimitA() > 1.8f);
}

void test_unlimited_mode_retains_four_amp_hardware_cap() {
    TwoWire wire(0);
    VoltageMonitor voltage;
    PDManager pdManager;
    initializeVoltage(voltage, pdManager);
    CurrentControl control;
    control.begin(wire, voltage);

    for (uint8_t step = 0; step < 100; ++step) {
        testNowMs += 75;
        control.update(1000.0f, false, 4.0f);
    }

    TEST_ASSERT_TRUE(control.getCurrentLimitA() <= 4.0f);
    TEST_ASSERT_TRUE(control.getCurrentLimitA() > 3.8f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_digipot_does_not_step_before_75_ms);
    RUN_TEST(test_digipot_increases_one_step_per_interval);
    RUN_TEST(test_final_three_steps_require_500_ms_of_continuous_demand);
    RUN_TEST(test_output_current_uses_average_calibration);
    RUN_TEST(test_limited_mode_caps_output_current_at_two_amps);
    RUN_TEST(test_unlimited_mode_retains_four_amp_hardware_cap);
    return UNITY_END();
}
