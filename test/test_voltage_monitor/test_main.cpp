#include "PDManager.h"
#include "VoltageMonitor.h"
#include "../test_support.h"
#include <unity.h>

void setUp() {
    testAdcReading = 0;
}

static float voltageChangeAfterOneSample(int initialAdc, int nextAdc) {
    PDManager pdManager;
    VoltageMonitor voltageMonitor;
    voltageMonitor.begin(1, pdManager);

    testAdcReading = initialAdc;
    voltageMonitor.update();
    const float initialVoltageV = voltageMonitor.getVoltage();

    testAdcReading = nextAdc;
    voltageMonitor.update();
    return voltageMonitor.getVoltage() - initialVoltageV;
}

void test_voltage_rises_significantly_faster_than_it_falls() {
    const float voltageIncreaseV = voltageChangeAfterOneSample(1600, 1900);
    const float voltageDecreaseV = -voltageChangeAfterOneSample(1900, 1600);

    TEST_ASSERT_TRUE(voltageIncreaseV > voltageDecreaseV * 4.0f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_voltage_rises_significantly_faster_than_it_falls);
    return UNITY_END();
}
