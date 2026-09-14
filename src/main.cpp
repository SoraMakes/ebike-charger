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

// TTGO T-Display I2C pins (SDA=21, SCL=22).
static constexpr int      I2C_SDA_PIN              = 21;
static constexpr int      I2C_SCL_PIN              = 22;
static constexpr float    kMinChargeVoltage        = 8.5f;  // Minimum voltage required to enable the charger (eg minimum voltage of the boost converter)

// TTGO T-Display: digital pins.
static constexpr uint8_t kFanPin  = 12;  // LEDC PWM fan driver (MOSFET)
static constexpr int8_t  kPdIntPin = 13; // PD trigger board (RotoPD Pro) INT output

static PDManager      pdManager;
static ThermalManager thermalManager;
static PowerMonitor   powerMonitor;
static CurrentControl currentControl;
static VoltageMonitor voltageMonitor;
static DisplayManager displayManager;
static EfficiencyEstimator efficiencyEstimator;

static constexpr float kElectronicsPowerW          = 2.0f;  // esp, sensors, fan  (measured: 1W over USB, fan 100%, no INA, no output current sensor)

// A periodic task owns its interval, timestamp, and the action to run once due.
// Unsigned subtraction remains safe across the millis() rollover.
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

// --- Task actions -----------------------------------------------------------

static void updateDisplay() {
    displayManager.update(pdManager, thermalManager, currentControl, voltageMonitor, powerMonitor, efficiencyEstimator);
}

static void updateThermal() {
    thermalManager.update();
}

static void logThermalStatus() {
    if (thermalManager.hasSensorFault()) {
        DiagLog::event("safety no temp");
    }
}

static void updatePd() {
    // INA238 PD-input volts feed liveness/accept checks.
    pdManager.setMeasuredInputVoltage(powerMonitor.getInputVoltage());
    pdManager.update();

    if (pdManager.justDetached()) {
        currentControl.safe();
    }

    if (pdManager.voltageJumped()) {
        currentControl.safe();
    }

    if (pdManager.justAttached()) {
        currentControl.safe();
    }
}

static void updateVoltage() {
    voltageMonitor.update();
}

static void updatePowerMonitor() {
    powerMonitor.update();
}

static void logNoSource() {
    DiagLog::event("no source");
}

// Periodic compact PD status (before early returns so faults stay visible).
static void logPdDiagnostics() {
    pdManager.logDiagnostics(powerMonitor.getInputVoltage(),
                             powerMonitor.getInputCurrent(),
                             voltageMonitor.getVoltage(),
                             currentControl.getDigipotStep(),
                             powerMonitor.getAlertText());
}

static void dumpDiagnosticHistory() {
    DiagLog::dump();
}

static void updateCurrentControl() {
    // Measured INA volts set budget; contract current is the target.
    const float inputVoltageV = powerMonitor.getInputVoltage();
    const float pdPowerLimit = pdManager.getCurrent() * inputVoltageV;
    const float pdOutputPowerLimit = calculateOutputPowerLimit(pdPowerLimit);
    const float thermalOutputPowerLimit = thermalManager.getOutputPowerLimitW();
    const float outputPowerLimit = min(pdOutputPowerLimit, thermalOutputPowerLimit);

    currentControl.update(outputPowerLimit);
}

// Feeds the efficiency estimator with the allowed vs. measured input power.
static void updateEfficiency() {
    // Measured INA volts/current; contract current is the allowed target.
    const float inputVoltageV = powerMonitor.getInputVoltage();
    const float allowedInputPowerW = pdManager.getCurrent() * inputVoltageV;
    const float actualInputPowerW  = inputVoltageV * powerMonitor.getInputCurrent();

    efficiencyEstimator.update(allowedInputPowerW - kElectronicsPowerW,
                               actualInputPowerW  - kElectronicsPowerW,
                               currentControl.isRampUpFinished());
}

// --- Task table: interval and action defined together ------------------------

static PeriodicTask displayTask(100, updateDisplay);
static PeriodicTask thermalUpdateTask(1000, updateThermal);
static PeriodicTask thermalLogTask(5000, logThermalStatus);
// 50ms PD tick for sub-second debounce/timeout constants.
static PeriodicTask pdUpdateTask(50, updatePd);
static PeriodicTask voltageUpdateTask(100, updateVoltage);
static PeriodicTask powerMonitorUpdateTask(50, updatePowerMonitor);
static PeriodicTask currentUpdateTask(50, updateCurrentControl);
static PeriodicTask efficiencyUpdateTask(50, updateEfficiency);
static PeriodicTask noSourceLogTask(1000, logNoSource);
static PeriodicTask pdDiagTask(2000, logPdDiagnostics);
static PeriodicTask diagHistoryTask(10000, dumpDiagnosticHistory);

void setup() {
    Serial.begin(115200);

    Serial.println("\n\n=== USB PD Charger ===");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Digipot powers at mid-scale: force safe step first.
    currentControl.begin(Wire, voltageMonitor, pdManager);

    displayManager.begin();
    efficiencyEstimator.begin();

    voltageMonitor.begin(33, currentControl);  // voltage-divider ADC input
    if (!thermalManager.begin(Wire, kFanPin, currentControl)) {
        DiagLog::event("safety temp sensor");
    }

    while (!pdManager.begin(Wire, kPdIntPin)) {
        displayManager.showMessage("PD controller\nnot found!");
        delay(2000);
    }

    if (!powerMonitor.begin()) {
        DiagLog::event("safety ina missing");
    }
}

void loop() {
    // Display always runs.
    displayTask.tick();

    // Thermal & fan always run, regardless of PD state.
    thermalUpdateTask.tick();
    thermalLogTask.tick();

    powerMonitorUpdateTask.tick();
    pdUpdateTask.tick();
    voltageUpdateTask.tick();

    pdDiagTask.tick();
    diagHistoryTask.tick();

    if (!pdManager.isReady()) {
        currentControl.safe();
        if (!pdManager.isAttached()) {
            noSourceLogTask.tick();
        }
        return;
    }

    const float voltage = pdManager.getVoltage();

    if (voltage < kMinChargeVoltage) {
        currentControl.safe();
        return;
    }

    currentUpdateTask.tick();
    efficiencyUpdateTask.tick();
}
