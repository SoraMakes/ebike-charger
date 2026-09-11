#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <AP33772S.h>

/// Negotiation state for the active USB-PD source.
enum class PDNegotiationState : uint8_t {
    Disconnected,        ///< No source attached.
    AttachSettle,        ///< Source just attached; letting the controller settle.
    Requesting,          ///< A PDO was requested; waiting for the contract to settle.
    Ready,               ///< A fixed-PDO contract is active.
};

/// Manages USB-PD source negotiation via the RotoPD Pro (AP33772S)
class PDManager {
public:
    PDManager() = default;

    /// Initializes the USB-PD controller on the specified I2C bus.
    /// @param wire I2C bus reference.
    /// @param intPin Optional GPIO connected to RotoPD Pro INT pin (-1 if unpinned).
    /// @return true when the controller is available on I2C.
    bool begin(TwoWire &wire, int8_t intPin = -1);

    /// Advances the negotiation state machine. Call this periodically.
    void update();

    /// Supplies an independently measured voltage for the rail the source is
    /// delivering (the INA238 bus voltage). Call this before every update().
    ///
    /// This exists because register 0x11 cannot be used for it on this board -
    /// see the note on getVoutVoltageV(). Every contract decision that used to
    /// read 0x11 now reads this instead. A value below kRailValidMinV, or a
    /// non-finite one, is treated as "no measurement available", and the checks
    /// that depend on it are skipped rather than failed: a dead sensor must not
    /// be able to reject a good contract or tear down a live one.
    void setMeasuredRailVoltage(float volts);

    /// Reports whether a USB-PD source is currently attached (may still be negotiating).
    bool isAttached() const;

    /// Reports whether a contract is active and ready for use.
    bool isReady() const;

    /// True while a source is attached but no contract exists yet and the
    /// firmware is still actively retrying to force one (AttachSettle, retries
    /// not exhausted). Used by the keep-alive load: an impatient source
    /// (powerbank) auto-shuts-down on low load, so the fan is run at full speed
    /// during this window to draw extra current and buy the retries time.
    bool isAttachRetrying() const;

    /// True once when a source becomes ready (initial contract or after re-negotiation).
    bool justAttached() const;

    /// True once when a previously ready source goes away.
    bool justDetached() const;

    /// True once when the active contract voltage changes while already ready
    /// (PSU raises or lowers the offered profile).
    bool voltageJumped() const;

    /// Returns the voltage of the active source contract in volts.
    float getVoltage() const;

    /// Returns the current limit of the active source contract in amps.
    float getCurrent() const;

    /// Returns register 0x11 in volts.
    ///
    /// DO NOT USE THIS FOR ANY DECISION. The datasheet defines 0x11 as VOUT at
    /// 80 mV/LSB, but on this board it does not track the PD rail: it sits at
    /// ~4.96 V while a 20 V contract is active and the INA238 reads 20.12 V on
    /// the same rail, and it reads 4.7 V (i.e. dropout) from a 5 V-only source.
    /// Whatever node it is on behaves like the RotoPD Pro's always-on 5 V rail.
    ///
    /// The consequence is that every threshold built on it was a constant:
    /// "VOUT >= 0.75 x contract" was FALSE for any contract above 6.6 V, so no
    /// contract above that could ever be accepted or held, on any supply, with
    /// any cable. Kept only so the value stays visible in [PDDIAG] and on the
    /// screen as evidence; use setMeasuredRailVoltage().
    float getVoutVoltageV();

    /// Permanently switches off the RotoPD Pro's VBUS switch and prevents
    /// further re-negotiation. Idempotent — safe to call multiple times.
    void shutdown();

    /// Prints a one-line [PDDIAG] dump of the controller's raw registers plus
    /// the sticky STATUS latch. Never reads STATUS itself (that register is
    /// read-clear and belongs to update()), so this is safe to call as often
    /// as you like.
    void logDiagnostics(float inaVoltageV, float inaCurrentA,
                        float outputVoltageV, int digipotStep,
                        const char *inaAlertText = "");

    /// Every STATUS bit observed since boot. STATUS is read-clear, so bits are
    /// OR-accumulated as they are polled; without this the evidence of a
    /// protection trip is destroyed by the very read that observes it.
    const char *getFaultText() const;

    /// True once any of UVP/OVP/OCP/OTP has ever been reported by the chip.
    bool hasChipFault() const;

private:
    /// Highest fixed-PDO voltage we are willing to request.
    static constexpr float kMaxRequestVoltageV = 28.0f;

    /// Below this, a rail measurement is treated as absent rather than as a
    /// dead rail. A source is attached whenever the state machine is past
    /// Disconnected, so the rail is at least vSafe5V; anything under 3 V means
    /// the sensor is missing or failed, not that the rail collapsed.
    static constexpr float kRailValidMinV = 3.0f;

    /// Lowest advertised PDO current worth requesting. The AP33772S encodes
    /// CURRENT_MAX in 4-bit buckets whose lowest, code 0, means "less than
    /// 1.24 A" - too little to charge with, and impossible to bound safely.
    static constexpr float kMinUsablePdoCurrentA = 1.25f;

    /// How often to poll/refresh the PDO table while in AttachSettle.
    static constexpr uint32_t kAttachSettlePollMs       = 150;
    /// How often to fire a raw PD_REQMSG re-request while in AttachSettle with
    /// no cached capabilities, to prompt the chip to issue Get_Source_Cap to
    /// the source. The AP33772S has no direct Get_Source_Cap command.
    static constexpr uint32_t kCapsReRequestMs          = 300;

    /// Requests the best eligible fixed PDO found in the cached table.
    /// @return true if a request was sent.
    bool requestBestPdo();

    /// Scans the cached PDO table for the highest fixed PDO <= kMaxRequestVoltageV.
    /// @return the 1-based PDO number (1..13) or 0 if none found.
    uint8_t findBestFixedPdo() const;

    /// Checks if a power source is physically attached via OPMODE / VBUS voltage.
    bool isSourcePhysicallyPresent();

    /// Applies CONFIG/MASK/SYSTEM once, after the controller reports STARTED=1.
    void applyStartupConfig();

    /// Reads the read-clear STATUS register through a single choke point and
    /// OR-accumulates every bit into _statusSticky. All STATUS reads must go
    /// through here, or events are silently destroyed.
    uint8_t pollStatus();

    /// Re-issues PD_REQMSG to rebuild a contract that was lost to a protection
    /// trip, a source-side hard reset or a chip restart. The datasheet requires
    /// the host to do this: the chip will not recover on its own.
    /// @param reason short text for the log.
    /// @return true when a fresh request was sent.
    bool attemptRecovery(const char *reason);

    AP33772S _pd;

    int8_t _intPin = -1;

    PDNegotiationState _state          = PDNegotiationState::Disconnected;
    uint32_t           _stateEnteredAt = 0;
    uint32_t           _lastStatusPollMs = 0;
    uint32_t           _lastPdoPollMs  = 0;
    uint32_t           _lastCapsReqMs  = 0; // last raw PD_REQMSG re-request time
    uint32_t           _absentSinceMs  = 0; // 0 = source present, else millis() of first absence
    uint8_t            _lastOpMode     = 0xFF; // last OPMODE read, for transition logging

    // Contract liveness. VOUT is compared against the contract voltage; when it
    // falls away the contract is dead even though the chip still reports a
    // source and the PDO table still advertises the same profile.
    uint32_t _contractLostSinceMs = 0; // 0 = healthy, else millis() of first low reading
    uint8_t  _recoveryAttempts    = 0;
    uint32_t _lastRecoveryMs      = 0;
    bool     _recoveryExhausted   = false;
    // Earliest millis() at which another PD_REQMSG may be sent. Keeps a source
    // that rejects our request from being hammered at the task rate.
    uint32_t _nextRequestAllowedMs = 0;

    uint8_t _activePdoIndex  = 0; // 1-based PDO number, 0 = none
    float   _voltage = 0.0f;
    float   _current = 0.0f;

    bool _justAttached  = false;
    bool _justDetached  = false;
    bool _voltageJumped = false;

    bool _shutdownLatched = false;
    bool _configured      = false; // CONFIG/MASK/SYSTEM applied after STARTED=1

    uint8_t _statusLast   = 0; // STATUS as of the most recent poll
    uint8_t _statusSticky = 0; // OR of every STATUS bit ever seen
    uint8_t _lastRailBand = 0xFF; // 0 = <1V, 1 = ~5V, 2 = high, 0xFF = unknown

    // Externally measured source rail voltage (INA238). See
    // setMeasuredRailVoltage(); replaces every former use of register 0x11.
    float _railVoltageV = 0.0f;
    bool  _railValid    = false;
    bool  _railWasValid = false; // for logging validity transitions once
};

