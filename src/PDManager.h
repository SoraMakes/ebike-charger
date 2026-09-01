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

    /// Reports whether a USB-PD source is currently attached (may still be negotiating).
    bool isAttached() const;

    /// Reports whether a contract is active and ready for use.
    bool isReady() const;

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

    /// Returns the power limit of the active source contract in watts.
    float getWattage() const;

    /// Reports whether the RotoPD Pro's VOUT MOS switch is actually conducting.
    /// Based on the measured VOUT voltage (>= 1V), NOT the SYSTEM register:
    /// the chip's own protections (UVP/OVP/OCP/OTP) turn VOUT off internally
    /// without changing the SYSTEM register, so only the measured voltage is
    /// a reliable indicator of the real output state.
    bool isOutputEnabled();

    /// Returns the AP33772S VOUT voltage in volts (register 0x11).
    ///
    /// NOTE: VOUT is the node feeding the voltage regulator's INPUT, not the
    /// charger's 42 V output. Reading ~5 V here while the contract claims 20 V
    /// means the contract died and the source fell back to vSafe5V, which the
    /// old boolean isOutputEnabled() reported as "enabled" either way.
    float getVoutVoltageV();

    /// Permanently switches off the RotoPD Pro's VBUS switch and prevents
    /// further re-negotiation. Idempotent — safe to call multiple times.
    void shutdown();

    /// Prints a one-line [PDDIAG] dump of the controller's raw registers plus
    /// the sticky STATUS latch. Never reads STATUS itself (that register is
    /// read-clear and belongs to update()), so this is safe to call as often
    /// as you like.
    void logDiagnostics(float inaVoltageV, float inaCurrentA,
                        float outputVoltageV, int digipotStep);

    /// Every STATUS bit observed since boot. STATUS is read-clear, so bits are
    /// OR-accumulated as they are polled; without this the evidence of a
    /// protection trip is destroyed by the very read that observes it.
    uint8_t getStickyStatus() const { return _statusSticky; }

    /// Short text naming the AP33772S protections that have tripped since
    /// boot ("OCP", "OVP+OTP", ...), or "" when none have.
    const char *getFaultText() const;

    /// True once any of UVP/OVP/OCP/OTP has ever been reported by the chip.
    bool hasChipFault() const;

    /// Current negotiation state, for display and logging.
    PDNegotiationState getState() const { return _state; }

private:
    /// Highest fixed-PDO voltage we are willing to request.
    static constexpr float kMaxRequestVoltageV = 28.0f;

    /// Lowest advertised PDO current worth requesting. The AP33772S encodes
    /// CURRENT_MAX in 4-bit buckets whose lowest, code 0, means "less than
    /// 1.24 A" - too little to charge with, and impossible to bound safely.
    static constexpr float kMinUsablePdoCurrentA = 1.25f;

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
    uint32_t           _absentSinceMs  = 0; // 0 = source present, else millis() of first absence

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
    uint8_t _lastVoutBand = 0xFF; // 0 = <1V, 1 = ~5V, 2 = high, 0xFF = unknown
};

