#include "PowerMonitor.h"
#include "../test_support.h"
#include "../mocks/INA238.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    clearTestSerialOutput();
    FakeINA238::reset();
}

static void failNextCurrentRead() {
    FakeINA238::state().failingRead = FakeINA238::Read::Current;
    FakeINA238::state().failuresRemaining = 1;
}

void test_faults_after_four_consecutive_failed_updates() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());

    for (uint8_t errorCount = 1; errorCount <= 3; ++errorCount) {
        failNextCurrentRead();
        monitor.update();
        TEST_ASSERT_FALSE(monitor.hasSensorFault());
        if (errorCount == 1) {
            TEST_ASSERT_TRUE(getTestSerialOutput().find("1/4") != std::string::npos);
        }
        if (errorCount == 2) {
            TEST_ASSERT_TRUE(getTestSerialOutput().find("2/4") != std::string::npos);
            clearTestSerialOutput();
        }
        if (errorCount == 3) {
            TEST_ASSERT_TRUE(getTestSerialOutput().find("3/4") != std::string::npos);
        }
    }

    failNextCurrentRead();
    monitor.update();

    TEST_ASSERT_TRUE(monitor.hasSensorFault());
}

void test_successful_update_resets_consecutive_error_count() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());

    for (uint8_t i = 0; i < 3; ++i) {
        failNextCurrentRead();
        monitor.update();
    }
    TEST_ASSERT_FALSE(monitor.hasSensorFault());

    FakeINA238::state().failingRead = FakeINA238::Read::None;
    monitor.update();
    clearTestSerialOutput();

    failNextCurrentRead();
    monitor.update();

    TEST_ASSERT_FALSE(monitor.hasSensorFault());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("1/4") != std::string::npos);
}

void test_failed_read_keeps_last_complete_measurements() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    monitor.update();

    FakeINA238::state().busVoltage = 21.0f;
    FakeINA238::state().current = 0.7f;
    failNextCurrentRead();
    monitor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, monitor.getInputVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, monitor.getInputCurrent());

    monitor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.0f, monitor.getInputVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.7f, monitor.getInputCurrent());
}

void test_negative_current_is_rejected_as_a_sensor_read_error() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    monitor.update();

    FakeINA238::state().current = -0.1f;
    for (uint8_t errorCount = 0; errorCount < 4; ++errorCount) {
        monitor.update();
    }

    TEST_ASSERT_TRUE(monitor.hasSensorFault());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, monitor.getInputVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, monitor.getInputCurrent());
}

void test_negative_voltage_is_rejected_as_a_sensor_read_error() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    monitor.update();

    FakeINA238::state().busVoltage = -20.0f;
    for (uint8_t errorCount = 0; errorCount < 4; ++errorCount) {
        monitor.update();
    }

    TEST_ASSERT_TRUE(monitor.hasSensorFault());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, monitor.getInputVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, monitor.getInputCurrent());
}

void test_temperature_is_reported_from_the_sensor() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    FakeINA238::state().temperatureC = 41.5f;

    monitor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 41.5f, monitor.getTemperatureC());
}

void test_failed_temperature_read_keeps_last_value() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    FakeINA238::state().temperatureC = 33.0f;
    monitor.update();

    FakeINA238::state().failingRead = FakeINA238::Read::Temperature;
    FakeINA238::state().failuresRemaining = 1;
    monitor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 33.0f, monitor.getTemperatureC());
}

void test_implausible_temperature_is_rejected_as_a_sensor_read_error() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());
    monitor.update();
    FakeINA238::state().temperatureC = 200.0f;

    for (uint8_t errorCount = 0; errorCount < 4; ++errorCount) {
        monitor.update();
    }

    TEST_ASSERT_TRUE(monitor.hasSensorFault());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, monitor.getTemperatureC());
}

void test_protection_events_log_only_the_current_trigger() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());

    FakeINA238::state().diagnostics = 1 << INA238_DIAG_SHUNT_OVER_LIMIT;
    monitor.update();
    TEST_ASSERT_TRUE(getTestSerialOutput().find("safety ina OCP") != std::string::npos);

    clearTestSerialOutput();
    FakeINA238::state().diagnostics = 1 << INA238_DIAG_BUS_OVER_LIMIT;
    monitor.update();
    TEST_ASSERT_TRUE(getTestSerialOutput().find("safety ina OVP") != std::string::npos);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("OCP") == std::string::npos);
}

void test_repeated_protection_occurrences_are_logged_again() {
    PowerMonitor monitor;
    TEST_ASSERT_TRUE(monitor.begin());

    FakeINA238::state().diagnostics = 1 << INA238_DIAG_SHUNT_OVER_LIMIT;
    monitor.update();
    clearTestSerialOutput();
    FakeINA238::state().diagnostics = 1 << INA238_DIAG_SHUNT_OVER_LIMIT;
    monitor.update();

    TEST_ASSERT_TRUE(getTestSerialOutput().find("safety ina OCP") != std::string::npos);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_faults_after_four_consecutive_failed_updates);
    RUN_TEST(test_successful_update_resets_consecutive_error_count);
    RUN_TEST(test_failed_read_keeps_last_complete_measurements);
    RUN_TEST(test_negative_current_is_rejected_as_a_sensor_read_error);
    RUN_TEST(test_negative_voltage_is_rejected_as_a_sensor_read_error);
    RUN_TEST(test_temperature_is_reported_from_the_sensor);
    RUN_TEST(test_failed_temperature_read_keeps_last_value);
    RUN_TEST(test_implausible_temperature_is_rejected_as_a_sensor_read_error);
    RUN_TEST(test_protection_events_log_only_the_current_trigger);
    RUN_TEST(test_repeated_protection_occurrences_are_logged_again);
    return UNITY_END();
}
