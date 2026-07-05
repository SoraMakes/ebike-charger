#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"

static constexpr int I2C_SDA_PIN = 33;
static constexpr int I2C_SCL_PIN = 35;
static constexpr uint32_t kSerialWaitMs = 3000;

static PDManager pdManager;
static PDMode    currentPDMode = PDMode::NO_CHARGER;

void setup() {
    Serial.begin(115200);

    const uint32_t deadline = millis() + kSerialWaitMs;
    while (!Serial && millis() < deadline) {
        delay(10);
    }

    Serial.println("\n\n=== USB PD Controller ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    if (!pdManager.begin(Wire)) {
        Serial.println("[SYSTEM] ERROR: PD controller not found on I2C. Halting.");
        while (true) {
            delay(1000);
        }
    }

    currentPDMode = pdManager.detectMode();

    Serial.println("────────────────────────────────────────────────");
    switch (currentPDMode) {
        case PDMode::NO_CHARGER:
            Serial.println("[SYSTEM] Mode 1: No charger detected.");
            break;
        case PDMode::FIVE_VOLT_ONLY:
            Serial.println("[SYSTEM] Mode 1: 5V only (PD unavailable).");
            break;
        case PDMode::HIGH_VOLTAGE:
            Serial.printf("[SYSTEM] Mode 3: Active (%.0fV @ %.2fA).\n",
                          pdManager.getNegotiatedVoltage(), pdManager.getNegotiatedCurrentAmps());
            break;
        default:
            break;
    }
    Serial.println("────────────────────────────────────────────────");
}

void loop() {
    switch (currentPDMode) {
        case PDMode::HIGH_VOLTAGE:
            Serial.println("[Mode 3] Active.");
            delay(5000);
            break;

        case PDMode::NO_CHARGER:
        case PDMode::FIVE_VOLT_ONLY:
            Serial.println("[Mode 1] Idle.");
            delay(10000);
            break;

        default:
            delay(1000);
            break;
    }
}
