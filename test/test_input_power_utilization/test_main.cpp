#include "InputPowerUtilizationController.h"
#include <unity.h>

void test_starts_at_initial_factor() {
    InputPowerUtilizationController controller;
    controller.begin();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.80f, controller.getOutputPowerFactor());
}

void test_holds_factor_until_ramp_finishes() {
    InputPowerUtilizationController controller;
    controller.begin();

    controller.update(100.0f, 50.0f, false, false);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.80f, controller.getOutputPowerFactor());
}

void test_increases_factor_after_ramp_below_target() {
    InputPowerUtilizationController controller;
    controller.begin();

    controller.update(100.0f, 50.0f, true, false);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.805f, controller.getOutputPowerFactor());
}

void test_reduces_factor_proportionally_above_target() {
    InputPowerUtilizationController controller;
    controller.begin();
    controller.update(100.0f, 50.0f, true, false);

    controller.update(100.0f, 100.0f, true, false);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.7889f, controller.getOutputPowerFactor());
}

void test_ignores_invalid_available_power() {
    InputPowerUtilizationController controller;
    controller.begin();
    controller.update(100.0f, 50.0f, true, false);

    controller.update(0.0f, 50.0f, true, false);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.805f, controller.getOutputPowerFactor());
}

void test_resets_factor_when_no_load() {
    InputPowerUtilizationController controller;
    controller.begin();
    controller.update(100.0f, 100.0f, true, false);

    controller.update(100.0f, 1.0f, true, true);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.80f, controller.getOutputPowerFactor());
}

void test_caps_factor_at_one() {
    InputPowerUtilizationController controller;
    controller.begin();

    for (int i = 0; i < 100; ++i) {
        controller.update(100.0f, 50.0f, true, false);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, controller.getOutputPowerFactor());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_at_initial_factor);
    RUN_TEST(test_holds_factor_until_ramp_finishes);
    RUN_TEST(test_increases_factor_after_ramp_below_target);
    RUN_TEST(test_reduces_factor_proportionally_above_target);
    RUN_TEST(test_ignores_invalid_available_power);
    RUN_TEST(test_resets_factor_when_no_load);
    RUN_TEST(test_caps_factor_at_one);
    return UNITY_END();
}
