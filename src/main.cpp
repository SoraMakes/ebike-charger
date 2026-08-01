#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"
// #include "ThermalManager.h"
// #include "PowerMonitor.h"
#include "CurrentControl.h"

static constexpr int      I2C_SDA_PIN              = 4;     // D2
static constexpr int      I2C_SCL_PIN              = 5;     // D1
static constexpr uint32_t kPdNegotiationTimeoutMs  = 10000;
static constexpr float    kMinChargeVoltage        = 7.0f;

// // TODO: assign real pins
// static constexpr uint8_t kNtcPin = A0;
// static constexpr uint8_t kFanPin = D5;

static PDManager      pdManager;
// static ThermalManager thermalManager;
// static PowerMonitor   powerMonitor;
static CurrentControl currentControl;

static bool isDue(uint32_t &lastRun, uint32_t intervalMs) {
    const uint32_t now = millis();
    if (now - lastRun >= intervalMs) {
        lastRun = now;
        return true;
    }
    return false;
}

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

void setup() {
    Serial.begin(115200);

    Serial.println("\n\n=== USB PD Charger ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    currentControl.begin(Wire);
    currentControl.safe();

    while (!pdManager.begin(Wire)) {
        Serial.println("[SYSTEM] ERROR: PD controller not found on I2C. Retrying...");
        delay(2000);
    }

    // thermalManager.begin(kNtcPin, kFanPin);


    // if (!powerMonitor.begin(Wire)) {
    //     Serial.println("[SYSTEM] WARNING: INA219 not found on I2C.");
    // }
}

void loop() {
    static bool     requestPending    = false;
    static uint32_t requestStartedAt  = 0;
    static bool     sourceWasAttached = false;
    static bool     sourceDetecting   = false;
    static uint32_t sourceDetectedAt  = 0;
    // static uint32_t lastThermalUpdate = 0;
    static uint32_t lastCurrentUpdate = 0;
    static uint32_t lastPdPrint       = 0;
    static uint32_t lastPdNegotiate   = 0;
    static uint32_t lastNoSourceLog   = 0;

    // // Thermal & fan always run, regardless of PD state
    // if (isDue(lastThermalUpdate, 500)) {
    //     thermalManager.update();
    //     Serial.printf("[THERM] %.1f°C\n", thermalManager.getTemperatureC());
    // }

    const float voltage = pdManager.getVoltage();

    if (!pdManager.isAttached() || voltage == 0.0f) {
        if (sourceWasAttached) {
            currentControl.safe();
            sourceWasAttached = false;
            requestPending    = false;
            Serial.println("[PD] Power source disconnected.");
        }
        if (isDue(lastNoSourceLog, 1000)) {
            Serial.println("[PD] No power source attached.");
            pdManager.resetAndRefreshSource();  // only for debugging; allows hot-plug
        }
        return;
    }

    // Debounce new source attachment
    if (!sourceWasAttached) {
        if (!sourceDetecting) {
            sourceDetecting  = true;
            sourceDetectedAt = millis();
            requestPending   = false;
            Serial.println("[PD] New power source detected; refreshing capabilities.");
        }
        if (millis() - sourceDetectedAt < 2000) return;
        sourceWasAttached = true;
        sourceDetecting   = false;
    }

    if (isDue(lastPdPrint, 2000)) {
        Serial.printf("[PD] %.0fV  %.2fA  %.1fW\n",
                      voltage, pdManager.getCurrent(), pdManager.getWattage());
    }

    if (voltage < kMinChargeVoltage) {
        currentControl.safe();
        if (isDue(lastPdNegotiate, 2000)) {
            handleBelowMinimumVoltage(requestPending, requestStartedAt);
        }
        return;
    }

    // Voltage is good — run current control every 100 ms
    if (isDue(lastCurrentUpdate, 100)) {
        // powerMonitor.update();

        const float pdPowerLimit = pdManager.getWattage();
        // const float thermalLimit = thermalManager.getCurrentLimit();
        // const float inputLimit   = powerMonitor.getCurrentLimit();
        // const float powerLimit  = min(pdPowerLimit, min(thermalLimit, inputLimit));
        const float powerLimit   = pdPowerLimit;

        currentControl.update(powerLimit);

        // Serial.printf("[CHARGER] limit=%.2fA  (pd=%.2f  therm=%.2f  input=%.2f)\n",
        //               limit, pdLimit, thermalLimit, inputLimit);
    }
}
