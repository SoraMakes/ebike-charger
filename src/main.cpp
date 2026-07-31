#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"

static constexpr int     I2C_SDA_PIN       = 33;
static constexpr int     I2C_SCL_PIN       = 35;
static constexpr uint32_t kSerialWaitMs    = 3000;
static constexpr float   kMinChargeVoltage = 7.0f;

static PDManager pdManager;

static void handleBelowMinimumVoltage(bool &requestPending) {
    if (!requestPending) {
        float requestedVoltage = 0.0f;
        float requestedCurrent = 0.0f;

        if (pdManager.requestHighestVoltagePdo(requestedVoltage,
                                                requestedCurrent)) {
            requestPending = true;
            Serial.printf("[PD] State: below minimum charge voltage, "
                          "higher voltage available. "
                          "Requesting %.0fV at %.2fA.\n",
                          requestedVoltage, requestedCurrent);
        } else {
            Serial.println("[PD] State: below minimum charge voltage; "
                           "no higher PDO available.");
        }
    } else if (pdManager.hasPDRequestFailed()) {
        requestPending = false;
        Serial.println("[PD] PD request failed; retrying...");
    } else {
        Serial.println("[PD] State: below minimum charge voltage, "
                       "higher voltage available. "
                       "Waiting for negotiation.");
    }
}

static void handleAboveMinimumVoltage(bool &requestPending,
                                      float voltage,
                                      float current) {
    requestPending = false;
    Serial.printf("[PD] State: higher voltage negotiated (%.0fV, %.2fA).\n",
                  voltage, current);
    Serial.println("[CHARGER] Turn output on.");
    Serial.println("[CHARGER] Adjust output current.");
}

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
    static bool requestPending = false;

    const float voltage = pdManager.getVoltage();
    const float current = pdManager.getCurrent();
    const float wattage = pdManager.getWattage();

    Serial.printf("[PD] %.0fV  %.2fA  %.1fW\n", voltage, current, wattage);

    if (!pdManager.isAttached()) {
        requestPending = false;
        Serial.println("[PD] No power source attached.");
        delay(2000);
        return;
    }

    if (voltage < kMinChargeVoltage) {
        handleBelowMinimumVoltage(requestPending);
    } else {
        handleAboveMinimumVoltage(requestPending, voltage, current);
    }

    delay(2000);
}
