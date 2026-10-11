#include "ChargingCurrentLimit.h"
#include "../mocks/Preferences.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testNowMs = 0;
    clearTestSerialOutput();
    Preferences::reset();
}

void test_defaults_to_two_amps_and_logs_boot_mode() {
    ChargingCurrentLimit limit;
    limit.begin();

    TEST_ASSERT_TRUE(limit.isLimited());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, limit.maximumCurrentA());
    TEST_ASSERT_TRUE(Preferences::state().hasSavedMode);
    TEST_ASSERT_TRUE(Preferences::state().limited);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit boot: limited 2A") != std::string::npos);
}

void test_restores_saved_unlimited_mode_and_logs_it() {
    Preferences::state().hasSavedMode = true;
    Preferences::state().limited = false;

    ChargingCurrentLimit limit;
    limit.begin();

    TEST_ASSERT_FALSE(limit.isLimited());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, limit.maximumCurrentA());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit boot: unlimited 4A") != std::string::npos);
}

void test_toggle_logs_and_persists_across_reinitialization() {
    ChargingCurrentLimit limit;
    limit.begin();
    clearTestSerialOutput();

    limit.toggle();
    TEST_ASSERT_FALSE(limit.isLimited());
    TEST_ASSERT_FALSE(Preferences::state().limited);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit: unlimited 4A") != std::string::npos);

    ChargingCurrentLimit rebootedLimit;
    rebootedLimit.begin();
    TEST_ASSERT_FALSE(rebootedLimit.isLimited());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit boot: unlimited 4A") != std::string::npos);

    clearTestSerialOutput();
    rebootedLimit.toggle();
    TEST_ASSERT_TRUE(rebootedLimit.isLimited());
    TEST_ASSERT_TRUE(Preferences::state().limited);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit: limited 2A") != std::string::npos);
}

void test_missing_storage_defaults_to_two_amps_and_logs_failure() {
    Preferences::state().beginSucceeds = false;

    ChargingCurrentLimit limit;
    limit.begin();

    TEST_ASSERT_TRUE(limit.isLimited());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, limit.maximumCurrentA());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit boot: limited 2A (NVS fail)") != std::string::npos);
    limit.toggle();
    TEST_ASSERT_FALSE(limit.isLimited());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, limit.maximumCurrentA());
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit: unlimited 4A") != std::string::npos);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit save failed") != std::string::npos);
}

void test_write_failure_still_applies_toggle_and_logs_failure() {
    Preferences::state().hasSavedMode = true;
    Preferences::state().limited = true;
    Preferences::state().writeSucceeds = false;

    ChargingCurrentLimit limit;
    limit.begin();
    clearTestSerialOutput();

    limit.toggle();
    TEST_ASSERT_FALSE(limit.isLimited());
    TEST_ASSERT_TRUE(Preferences::state().limited);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit save failed") != std::string::npos);
    TEST_ASSERT_TRUE(getTestSerialOutput().find("current limit: unlimited 4A") != std::string::npos);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_to_two_amps_and_logs_boot_mode);
    RUN_TEST(test_restores_saved_unlimited_mode_and_logs_it);
    RUN_TEST(test_toggle_logs_and_persists_across_reinitialization);
    RUN_TEST(test_missing_storage_defaults_to_two_amps_and_logs_failure);
    RUN_TEST(test_write_failure_still_applies_toggle_and_logs_failure);
    return UNITY_END();
}
