#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "PDManager.h"
#include "ThermalManager.h"
#include "PowerMonitor.h"
#include "CurrentControl.h"
#include "VoltageMonitor.h"
#include "DisplayManager.h"
#include "EfficiencyEstimator.h"
#include "DiagLog.h"

// TTGO T-Display I2C pins.
static constexpr int      I2C_SDA_PIN              = 21;
static constexpr int      I2C_SCL_PIN              = 22;
static constexpr float    kMinChargeVoltage        = 8.5f;  // Charger enable floor.
static constexpr size_t   kSerialTxBufferSizeBytes = 2048;

// Digital pins.
static constexpr uint8_t kFanPin  = 12;  // Fan MOSFET driver.
static constexpr int8_t  kPdIntPin = 13; // RotoPD Pro INT output.

static PDManager      pdManager;
static ThermalManager thermalManager;
static PowerMonitor   powerMonitor;
static CurrentControl currentControl;
static VoltageMonitor voltageMonitor;
static DisplayManager displayManager;
static EfficiencyEstimator efficiencyEstimator;

static constexpr float kElectronicsPowerW          = 2.0f;  // Electronics overhead.
static constexpr float kNoLoadInputW               = 5.0f;  // Absent-load floor.

// Rollover-safe interval task.
class PeriodicTask {
public:
    PeriodicTask(uint32_t intervalMs, void (*action)()) : _intervalMs(intervalMs), _action(action) {}

    void tick() {
        const uint32_t now = millis();
        if (now - _lastRun >= _intervalMs) {
            _lastRun = now;
            _action();
        }
    }

private:
    const uint32_t _intervalMs;
    void (*const _action)();
    uint32_t _lastRun = 0;
};

static float calculateOutputPowerLimit(float inputPowerLimitW) {
    if (inputPowerLimitW <= kElectronicsPowerW) {
        return 0.0f;
    }

    const float regulatorInputPowerW = inputPowerLimitW - kElectronicsPowerW;
    return regulatorInputPowerW * efficiencyEstimator.getEfficiency();
}

static void updateDisplay() {
    displayManager.update(pdManager, thermalManager, currentControl, voltageMonitor, powerMonitor, efficiencyEstimator);
}

static void updateThermal() {
    thermalManager.update();
}

static void updatePd() {
    // PD-input volts for contract checks.
    pdManager.setMeasuredInputVoltage(powerMonitor.getInputVoltage());
    pdManager.update();

    if (pdManager.voltageJumped()) {
        currentControl.minimumCurrent();
    }

    if (pdManager.justAttached()) {
        currentControl.minimumCurrent();
    }
}

static void updateVoltage() {
    voltageMonitor.update();
}

static void updatePowerMonitor() {
    powerMonitor.update();
}

// PD plus thermal status.
static void logSystemDiagnostics() {
    static const char *kStateNames[] = {"DISC", "SETTLE", "REQ", "READY"};
    const char *inaAlert = powerMonitor.getAlertText();
    const bool hasInaAlert = (inaAlert != nullptr && inaAlert[0] != '\0');
    Serial.printf("[PD] st=%s contract=%.1fV/%.2fA opmode=0x%02X msgrlt=%u sticky=0x%02X%s%s%s%s ina=%.2fV/%.2fA out=%.2fV pot=%d temp=%.1fC thlim=%.1fW\n",
        kStateNames[static_cast<uint8_t>(pdManager.getState())],
        pdManager.getVoltage(), pdManager.getCurrent(),
        pdManager.getOpMode(), pdManager.getMsgResult(), pdManager.getStickyStatus(),
        pdManager.hasChipFault() ? " FAULT:" : "", pdManager.getFaultText(),
        hasInaAlert ? " INA:" : "", hasInaAlert ? inaAlert : "",
        powerMonitor.getInputVoltage(), powerMonitor.getInputCurrent(),
        voltageMonitor.getVoltage(), currentControl.getDigipotStep(),
        thermalManager.getTemperatureC(), thermalManager.getOutputPowerLimitW());
}

static void dumpDiagnosticHistory() {
    DiagLog::dump();
}

static void updateCurrentControl() {
    // INA volts budget; contract current target.
    const float inputVoltageV = powerMonitor.getInputVoltage();
    const float pdPowerLimit = pdManager.getCurrent() * inputVoltageV;
    const float pdOutputPowerLimit = calculateOutputPowerLimit(pdPowerLimit);
    const float thermalOutputPowerLimit = thermalManager.getOutputPowerLimitW();
    const float outputPowerLimit = min(pdOutputPowerLimit, thermalOutputPowerLimit);
    const float actualInputPowerW = inputVoltageV * powerMonitor.getInputCurrent();
    const bool noLoad = actualInputPowerW < kNoLoadInputW;

    currentControl.update(outputPowerLimit, noLoad);
}

static void updateEfficiency() {
    // INA volts/current; contract current is allowed.
    const float inputVoltageV = powerMonitor.getInputVoltage();
    const float allowedInputPowerW = pdManager.getCurrent() * inputVoltageV;
    const float actualInputPowerW  = inputVoltageV * powerMonitor.getInputCurrent();

    const bool noLoad = actualInputPowerW < kNoLoadInputW;

    efficiencyEstimator.update(allowedInputPowerW - kElectronicsPowerW,
                               actualInputPowerW  - kElectronicsPowerW,
                               currentControl.isRampUpFinished(),
                               noLoad);
}

static PeriodicTask displayTask(100, updateDisplay);
static PeriodicTask thermalUpdateTask(1000, updateThermal);
// Sub-second debounce/timeout tick.
static PeriodicTask pdUpdateTask(50, updatePd);
static PeriodicTask voltageUpdateTask(100, updateVoltage);
static PeriodicTask powerMonitorUpdateTask(50, updatePowerMonitor);
static PeriodicTask currentUpdateTask(50, updateCurrentControl);
static PeriodicTask efficiencyUpdateTask(50, updateEfficiency);
static PeriodicTask pdDiagTask(2000, logSystemDiagnostics);
static PeriodicTask diagHistoryTask(10000, dumpDiagnosticHistory);

void setup() {
    Serial.setTxBufferSize(kSerialTxBufferSizeBytes);
    Serial.begin(115200);

    Serial.println("\n\n=== USB PD Charger ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Digipot powers mid-scale.
    currentControl.begin(Wire, voltageMonitor);

    displayManager.begin();
    efficiencyEstimator.begin();

    // Shutdown targets before sensors.
    while (!pdManager.begin(Wire, kPdIntPin)) {
        displayManager.showMessage("PD controller\nnot found!");
        delay(2000);
    }

    voltageMonitor.begin(33, pdManager);  // Divider ADC input.
    if (!thermalManager.begin(Wire, kFanPin, pdManager)) {
        DiagLog::event("safety temp sensor");
    }

    if (!powerMonitor.begin()) {
        // No rail truth without INA.
        pdManager.shutdown("ina missing");
    }
}

void loop() {
    displayTask.tick();
    thermalUpdateTask.tick();

    powerMonitorUpdateTask.tick();
    pdUpdateTask.tick();
    voltageUpdateTask.tick();

    pdDiagTask.tick();
    diagHistoryTask.tick();

    if (pdManager.isShutdownLatched()) {
        currentControl.minimumCurrent();
        return;
    }

    if (!pdManager.isReady()) {
        currentControl.minimumCurrent();
        return;
    }

    const float voltage = pdManager.getVoltage();

    if (voltage < kMinChargeVoltage) {
        currentControl.minimumCurrent();
        return;
    }

    currentUpdateTask.tick();
    efficiencyUpdateTask.tick();
}
