#include "ThermalManager.h"
#include "PDManager.h"
#include "DiagLog.h"
#include "../mocks/Adafruit_BMP280.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    clearTestSerialOutput();
    Adafruit_BMP280::reset();
}

void test_temperature_sensor_recovers_after_one_failed_read() {
    PDManager pd;
    ThermalManager thermal;
    thermal.begin(12, pd);

    Adafruit_BMP280::state().temperatureC = NAN;
    thermal.update();

    TEST_ASSERT_TRUE(thermal.hasSensorFault());
    TEST_ASSERT_FALSE(pd.isShutdownLatched());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("temp read error 1/2") != std::string::npos);

    Adafruit_BMP280::state().temperatureC = 25.0f;
    thermal.update();

    TEST_ASSERT_FALSE(thermal.hasSensorFault());
    TEST_ASSERT_FALSE(pd.isShutdownLatched());
}

void test_temperature_sensor_logs_first_failure_and_shuts_down_on_second() {
    PDManager pd;
    ThermalManager thermal;
    thermal.begin(12, pd);
    Adafruit_BMP280::state().temperatureC = NAN;

    thermal.update();
    TEST_ASSERT_FALSE(pd.isShutdownLatched());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("temp read error 1/2") != std::string::npos);

    thermal.update();
    TEST_ASSERT_TRUE(pd.isShutdownLatched());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("temp read error 2/2") != std::string::npos);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, thermal.getOutputPowerLimitFactor());
}

void test_successful_read_resets_consecutive_failure_count() {
    PDManager pd;
    ThermalManager thermal;
    thermal.begin(12, pd);
    Adafruit_BMP280::state().temperatureC = NAN;

    thermal.update();
    Adafruit_BMP280::state().temperatureC = 25.0f;
    thermal.update();

    Adafruit_BMP280::state().temperatureC = NAN;
    clearTestSerialOutput();
    thermal.update();
    TEST_ASSERT_TRUE(getTestSerialOutput().find("temp read error 1/2") != std::string::npos);

    thermal.update();
    TEST_ASSERT_TRUE(pd.isShutdownLatched());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("temp read error 2/2") != std::string::npos);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temperature_sensor_recovers_after_one_failed_read);
    RUN_TEST(test_temperature_sensor_logs_first_failure_and_shuts_down_on_second);
    RUN_TEST(test_successful_read_resets_consecutive_failure_count);
    return UNITY_END();
}
