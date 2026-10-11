#include "ThermalPowerDerating.h"
#include <unity.h>

void test_no_derating_at_or_below_start_temperature() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f,
                             ThermalPowerDerating::limitFactorForTemperatureC(35.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f,
                             ThermalPowerDerating::limitFactorForTemperatureC(40.0f));
}

void test_derating_scales_linearly_to_shutdown() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f,
                             ThermalPowerDerating::limitFactorForTemperatureC(45.0f));
}

void test_derating_reaches_zero_at_shutdown() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f,
                             ThermalPowerDerating::limitFactorForTemperatureC(50.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f,
                             ThermalPowerDerating::limitFactorForTemperatureC(55.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_derating_at_or_below_start_temperature);
    RUN_TEST(test_derating_scales_linearly_to_shutdown);
    RUN_TEST(test_derating_reaches_zero_at_shutdown);
    return UNITY_END();
}
