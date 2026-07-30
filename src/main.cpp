#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"

static constexpr int     I2C_SDA_PIN       = 33;
static constexpr int     I2C_SCL_PIN       = 35;
static constexpr uint32_t kSerialWaitMs    = 3000;
static constexpr float   kMinChargeVoltage = 7.0f;  // TODO: enable boost converter above this threshold

static PDManager pdManager;

void setup() {
    Serial.begin(115200);

    const uint32_t deadline = millis() + kSerialWaitMs;
    while (!Serial && millis() < deadline) {
        delay(10);
    }

    Serial.println("\n\n=== USB PD Charger ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    while (!pdManager.begin(Wire)) {
        Serial.println("[SYSTEM] ERROR: PD controller not found on I2C. Retrying...");
        delay(2000);
    }
}

void loop() {
    const float voltage = pdManager.getVoltage();
    const float current = pdManager.getCurrent();
    const float wattage = pdManager.getWattage();

    Serial.printf("[PD] %.0fV  %.2fA  %.1fW\n", voltage, current, wattage);

    // TODO: enable boost converter when voltage is sufficient for charging
    // if (voltage >= kMinChargeVoltage) { ... }

    delay(2000);
}
