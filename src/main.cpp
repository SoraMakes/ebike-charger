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
        Serial.println("[THERM] temperature unavailable; charging disabled");
    } else {
        Serial.printf("[THERM] temperature=%.1fC thermal-limit=%.1fW%s\n",
                      thermalManager.getTemperatureC(),
                      thermalManager.getOutputPowerLimitW(),
                      thermalManager.isShutdownLatched() ? " shutdown-latched" : "");
    }
}

static void updatePd() {
    // The PD state machine has no usable voltage sensor of its own: register
    // 0x11 does not track the PD rail on this board (see
    // PDManager::getVoutVoltageV). The INA238 is the only sensor that sees it,
    // so it has to be handed in. powerMonitorUpdateTask runs before
    // pdUpdateTask in loop() so this is the current reading, not last tick's.
    pdManager.setMeasuredRailVoltage(powerMonitor.getInputVoltage());

    pdManager.update();

    // Keep-alive: while a source is attached but no contract exists and the
    // firmware is still retrying to force one, run the fan at full speed so an
    // impatient powerbank does not auto-shut-down on low load before the
    // re-attach retries can catch its capabilities.
    thermalManager.setKeepAlive(pdManager.isAttachRetrying());

    if (pdManager.justDetached()) {
        currentControl.safe();
        Serial.println("[PD] Power source disconnected.");
    }

    if (pdManager.voltageJumped()) {
        currentControl.safe();
    }

    if (pdManager.justAttached()) {
        currentControl.safe();
        Serial.println("[PD] Contract active; output stays at the safe setpoint until it settles.");
    }
}

static void updateVoltage() {
    voltageMonitor.update();
}

static void updatePowerMonitor() {
    powerMonitor.update();
}

static void logNoSource() {
    Serial.println("[PD] No power source attached.");
}

// Repeats forever, so the state is still readable when a PC is attached long
// after the fault. Must run unconditionally, ahead of every early return in
// loop() - the failure being chased is precisely one where the loop bails out.
static void logPdDiagnostics() {
    pdManager.logDiagnostics(powerMonitor.getInputVoltage(),
                             powerMonitor.getInputCurrent(),
                             voltageMonitor.getVoltage(),
                             currentControl.getDigipotStep(),
                             powerMonitor.getAlertText());
}

// Re-prints the retained event ring buffer, including events recorded before a
// brownout reset. Slower than the one-line dump because it is many lines.
static void dumpDiagnosticHistory() {
    DiagLog::dump();
}

// The voltage to bill the PD power budget against. Prefer the measured rail,
// but never let a missing sensor decide the budget: the INA238 library returns
// 0.0 on any I2C failure, which zeroed pdPowerLimit -> outputPowerLimit -> a
// permanent safe() with no diagnostic anywhere. The contract voltage is what
// the source actually promised, so it is the correct fallback.
static float budgetVoltageV() {
    const float measured = powerMonitor.getInputVoltage();
    if (isfinite(measured) && measured >= 3.0f) {
        return measured;
    }
    return pdManager.getVoltage();
}

static void updateCurrentControl() {
    const float pdPowerLimit = pdManager.getCurrent() * budgetVoltageV();
    const float pdOutputPowerLimit = calculateOutputPowerLimit(pdPowerLimit);
    const float thermalOutputPowerLimit = thermalManager.getOutputPowerLimitW();
    const float outputPowerLimit = min(pdOutputPowerLimit, thermalOutputPowerLimit);

    currentControl.update(outputPowerLimit);
}

// Feeds the efficiency estimator with the allowed vs. measured input power.
static void updateEfficiency() {
    const float inputVoltageV = budgetVoltageV();
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
// 50 ms, not 1 s. PDManager's own constants (attach settle 300 ms, status poll
// 100 ms, profile recheck 500 ms, detach debounce 500 ms, contract-lost debounce
// 300 ms) are all sub-second, so at a 1 s tick every one of them was TRUE on
// every call and the effective period of everything was 1 s.
//
// The reason is REACTION LATENCY, not the read-clear STATUS register: read-clear
// bits latch in the chip until something reads them, so a slow poll only delays
// detection, it does not lose anything. Polling faster actually destroys them
// sooner - which is why this rate is only safe now that pollStatus() accumulates
// every bit into _statusSticky first. What the speed buys is the ability to see
// the chip's 30 ms protection debounce and a PD negotiation that completes in
// tens of milliseconds, plus a chance at the ~100 ms post-STARTED window that
// guide 8.2 allows for writing CONFIG/MASK/SYSTEM.
//
// It also depends on the time-based detach debounce in PDManager: the old code
// declared a detach on a single failed presence test, which at 20 Hz would have
// been 20 chances per second to spuriously drop VBUS.
static PeriodicTask pdUpdateTask(50, updatePd);
static PeriodicTask voltageUpdateTask(100, updateVoltage);
static PeriodicTask powerMonitorUpdateTask(50, updatePowerMonitor);
static PeriodicTask currentUpdateTask(50, updateCurrentControl);
static PeriodicTask efficiencyUpdateTask(50, updateEfficiency);
static PeriodicTask noSourceLogTask(1000, logNoSource);
static PeriodicTask pdDiagTask(2000, logPdDiagnostics);
static PeriodicTask diagHistoryTask(1000, dumpDiagnosticHistory);

void setup() {
    Serial.begin(115200);

    Serial.println("\n\n=== USB PD Charger ===");

    // Before anything else: report why we are booting and replay whatever the
    // previous boot managed to record before it died.
    DiagLog::begin();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // FIRST thing on the bus: force the digipot to its minimum-current step.
    //
    // The MCP4018 has a volatile wiper that powers up at MID-SCALE, and it sits
    // on the same 3V3 rail as the ESP. So after a brownout reset the digipot is
    // commanding roughly half of kMaxAllowedOutputAmps while the AP33772S - on
    // its own VBUS-derived supply, and therefore NOT reset - is still holding
    // VOUT on at the full contract voltage. The regulator is driven hard for the
    // whole boot, which is exactly the load step that can trip a marginal PSU
    // and cause the next brownout.
    //
    // This closes the setup() part of that window. It does NOT close the ESP32
    // bootloader part (~200-300 ms before setup() runs at all), which firmware
    // cannot reach - see the note in README about giving the regulator a real
    // enable line.
    currentControl.begin(Wire, voltageMonitor, pdManager);

    displayManager.begin();
    // Immediately: the reset reason, on the only channel that survives a boot
    // loop. The main display overwrites this on its first 500 ms tick, and
    // keeps the same information on the bottom line from then on.
    displayManager.showBootBanner(DiagLog::bootCount(), DiagLog::resetReasonText());
    efficiencyEstimator.begin();

    voltageMonitor.begin(33, currentControl);  // TTGO T-Display: voltage-divider ADC input
    if (!thermalManager.begin(Wire, kFanPin, currentControl)) {
        Serial.println("[THERM] AHT10 unavailable; charging disabled.");
    }

    while (!pdManager.begin(Wire, kPdIntPin)) {
        Serial.println("[SYSTEM] ERROR: PD controller not found on I2C. Retrying...");
        displayManager.showMessage("PD controller\nnot found!");
        delay(2000);
    }

    if (!powerMonitor.begin()) {
        Serial.println("[SYSTEM] WARNING: INA238 not found on I2C.");
    }
}

void loop() {
    // Display always runs.
    displayTask.tick();

    // Thermal & fan always run, regardless of PD state.
    thermalUpdateTask.tick();
    thermalLogTask.tick();

    // powerMonitorUpdateTask MUST run before pdUpdateTask: the PD state machine
    // now takes its rail voltage from the INA238 via
    // PDManager::setMeasuredRailVoltage(), and both run at 50 ms, so the wrong
    // order would feed it a reading one full tick stale on every pass.
    powerMonitorUpdateTask.tick();
    pdUpdateTask.tick();
    voltageUpdateTask.tick();

    // Diagnostics run before every early return below, so a latched fault keeps
    // reporting itself for as long as the board stays powered.
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
