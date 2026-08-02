#include <Arduino.h>
#include <Wire.h>
#include "PDManager.h"
#include "ThermalManager.h"
// #include "PowerMonitor.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"

static constexpr int      I2C_SDA_PIN              = 4;     // D2
static constexpr int      I2C_SCL_PIN              = 5;     // D1
static constexpr float    kMinChargeVoltage        = 8.5f;  // Minimum voltage required to enable the charger (eg minimum voltage of the boost converter)

static constexpr uint8_t kFanPin = D6;  // GPIO12; D5 is used by CurrentControl

static PDManager      pdManager;
static ThermalManager thermalManager;
// static PowerMonitor   powerMonitor;
static CurrentControl currentControl;
static VoltageMonitor voltageMonitor;

static constexpr float kVoltageRegulatorEfficiency = 0.88f;  // calculated efficiency at 20V/5A: 90%
static constexpr float kElectronicsPowerW          = 2.0f;  // esp, sensors, fan  (measured: 1W over USB, fan 100%, no INA, no output current sensor)

static bool isDue(uint32_t &lastRun, uint32_t intervalMs) {
    const uint32_t now = millis();
    if (now - lastRun >= intervalMs) {
        lastRun = now;
        return true;
    }
    return false;
}

static float calculateOutputPowerLimit(float inputPowerLimitW) {
    if (inputPowerLimitW <= kElectronicsPowerW) {
        return 0.0f;
    }

    const float regulatorInputPowerW = inputPowerLimitW - kElectronicsPowerW;
    return regulatorInputPowerW * kVoltageRegulatorEfficiency;
}

void setup() {
    Serial.begin(115200);

    Serial.println("\n\n=== USB PD Charger ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    voltageMonitor.begin(A0);
    currentControl.begin(Wire, voltageMonitor);
    if (!thermalManager.begin(Wire, kFanPin)) {
        Serial.println("[THERM] AHT10 unavailable; charging disabled.");
    }

    while (!pdManager.begin(Wire)) {
        Serial.println("[SYSTEM] ERROR: PD controller not found on I2C. Retrying...");
        delay(2000);
    }

    // if (!powerMonitor.begin(Wire)) {
    //     Serial.println("[SYSTEM] WARNING: INA219 not found on I2C.");
    // }
}

void loop() {
    static uint32_t lastThermalUpdate = 0;
    static uint32_t lastThermalLog    = 0;
    static uint32_t lastPdUpdate      = 0;
    static uint32_t lastCurrentUpdate = 0;
    static uint32_t lastPowerLimitLog = 0;
    static uint32_t lastNoSourceLog   = 0;

    // Thermal & fan always run, regardless of PD state.
    if (isDue(lastThermalUpdate, 1000)) {
        thermalManager.update();
    }

    if (isDue(lastThermalLog, 5000)) {
        if (thermalManager.hasSensorFault()) {
            Serial.println("[THERM] temperature unavailable; charging disabled");
        } else {
            Serial.printf("[THERM] temperature=%.1fC thermal-limit=%.1fW%s\n",
                          thermalManager.getTemperatureC(),
                          thermalManager.getOutputPowerLimitW(),
                          thermalManager.isShutdownLatched() ? " shutdown-latched" : "");
        }
    }

    // For safety reasons. The power limit from thermalManager should be 0 too, causing the same effect
    // but this way it ensures power down eg even if a software bug would otherwise the limit not to be applied
    if (thermalManager.hasSensorFault() || thermalManager.isShutdownLatched()) {
        currentControl.safe();
        return;
    }

    if (isDue(lastPdUpdate, 250)) {
        pdManager.update();

        if (pdManager.justDetached()) {
            currentControl.safe();
            Serial.println("[PD] Power source disconnected.");
        }

        if (pdManager.voltageJumped()) {
            currentControl.safe();
        }

        if (pdManager.justAttached()) {
            currentControl.safe();
            Serial.println("[PD] Voltage settled; waiting 100 ms for output to stabilize.");
            delay(100);
        }
    }

    if (!pdManager.isReady()) {
        currentControl.safe();
        if (!pdManager.isAttached() && isDue(lastNoSourceLog, 1000)) {
            Serial.println("[PD] No power source attached.");
        }
        return;
    }

    const float voltage = pdManager.getVoltage();

    if (voltage < kMinChargeVoltage) {
        currentControl.safe();
        return;
    }

    if (isDue(lastCurrentUpdate, 100)) {
        // powerMonitor.update();

        const float pdPowerLimit = pdManager.getWattage();
        // const float thermalLimit = thermalManager.getCurrentLimit();
        // const float inputLimit   = powerMonitor.getCurrentLimit();
        // const float outputPowerLimit = min(pdOutputPowerLimit, min(thermalLimit, inputLimit));
        const float pdOutputPowerLimit = calculateOutputPowerLimit(pdPowerLimit);
        const float thermalOutputPowerLimit = thermalManager.getOutputPowerLimitW();
        const float outputPowerLimit = min(pdOutputPowerLimit, thermalOutputPowerLimit);

        currentControl.update(outputPowerLimit);

        // Log the current and power limit
        if (isDue(lastPowerLimitLog, 5000)) {
            const float outputVoltageV = voltageMonitor.getVoltage();
            const float outputCurrentLimitA = outputVoltageV > 0.0f
                ? outputPowerLimit / outputVoltageV
                : 0.0f;
            Serial.printf("[CHARGER LIMIT] current=%.2fA power=%.2fW (PD input=%.2fW thermal=%.1fW temp=%.1fC)\n",
                          outputCurrentLimitA, outputPowerLimit, pdPowerLimit,
                          thermalOutputPowerLimit, thermalManager.getTemperatureC());
        }
    }
}
