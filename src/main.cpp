#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"

static constexpr int     I2C_SDA_PIN       = 33;
static constexpr int     I2C_SCL_PIN       = 35;
static constexpr uint32_t kSerialWaitMs    = 3000;
static constexpr uint32_t kPdNegotiationTimeoutMs = 10000;
static constexpr float   kMinChargeVoltage = 7.0f;

static PDManager pdManager;

static void handleBelowMinimumVoltage(bool &requestPending, uint32_t &requestStartedAt) {
    if (!requestPending) {
        float requestedVoltage = 0.0f;
        float requestedCurrent = 0.0f;

        if (pdManager.requestHighestVoltagePdo(requestedVoltage,
                                                requestedCurrent)) {
            requestPending = true;
            requestStartedAt = millis();
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
    } else if (millis() - requestStartedAt >= kPdNegotiationTimeoutMs) {
        requestPending = false;
        Serial.println("[PD] PD negotiation timed out; retrying...");
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
    static uint32_t requestStartedAt = 0;
    static bool sourceWasAttached = false;

    const float voltage = pdManager.getVoltage();

    if (!pdManager.isAttached() || voltage == 0.0f) {
        sourceWasAttached = false;
        requestPending = false;
        Serial.println("[PD] No power source attached.");
        delay(1000);
        return;
    }

    if (!sourceWasAttached) {
        pdManager.resetAndRefreshSource();
        sourceWasAttached = true;
        requestPending = false;
        Serial.println("[PD] New power source detected; refreshing capabilities.");
        delay(2000);
        return;
    }

    const float current = pdManager.getCurrent();
    const float wattage = pdManager.getWattage();

    Serial.printf("[PD] %.0fV  %.2fA  %.1fW\n", voltage, current, wattage);

    if (voltage < kMinChargeVoltage) {
        handleBelowMinimumVoltage(requestPending, requestStartedAt);
    } else {
        handleAboveMinimumVoltage(requestPending, voltage, current);
    }

    delay(2000);
}
