#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <AP33772S.h>

enum class PDNegotiationState : uint8_t {
    Disconnected,
    WaitingForSourceCapabilities,
    Requesting,
    Ready,
};

class PDManager {
public:
    PDManager() = default;
    bool begin(TwoWire &wire, int8_t intPin);
    void update();

    /// Sets the measured USB-PD input voltage in volts; this is the PD input rail, not the 5 V auxiliary supply.
    void setMeasuredInputVoltage(float volts);
    bool justAttached() const;
    /// True on an update pass that starts contract renegotiation or recovery.
    bool contractChanged() const;
    /// Active contract voltage, or zero unless the contract is ready.
    float getVoltage() const;
    /// Active contract current, or zero unless the contract is ready.
    float getCurrent() const;
    /// Latched cut-out with reason.
    void shutdown(const char *reason = nullptr);
    bool isShutdownLatched() const { return _shutdownLatched; }
    const char *getShutdownReason() const { return _shutdownReason; }

    PDNegotiationState getState() const { return _state; }
    uint8_t getStickyStatus() const { return _statusSticky; }
    uint8_t getOpMode() { return _pd.readOpMode(); }
    uint8_t getMsgResult() { return static_cast<uint8_t>(_pd.readMsgResult() & 0x07); }
    /// Return fault names from sticky STATUS, or an empty string if none are latched.
    const char *getProtectionFaultText() const;
    /// True when sticky STATUS contains an AP33772S protection fault.
    bool hasProtectionFault() const;

private:
    // Minimum PD voltage supported by the boost converter.
    static constexpr float kMinChargeVoltageV = 8.5f;
    // Upper voltage limit requested from a source.
    static constexpr float kMaxRequestVoltageV = 28.0f;
    // Relative rail tolerance for Ready-state contract monitoring.
    static constexpr float kVoltageToleranceFraction = 0.10f;
    // PDO-table refresh interval while waiting for attachment updates.
    static constexpr uint32_t kSourceCapabilitiesPollMs = 150;

    /// Request the best usable fixed PDO and store its voltage/current.
    /// @return true if sent; false if no usable PDO exists or the request fails.
    bool requestBestPdo();
    /// Select the highest-voltage fixed PDO within the supported request range.
    /// @return the 1-based PDO index, or 0 if no usable PDO exists.
    uint8_t findBestFixedPdo() const;
    /// Apply controller configuration once after STARTED.
    void applyStartupConfig();
    /// Read STATUS, retain sticky flags, and log newly observed protection faults.
    uint8_t pollStatus();
    bool isReady() const;
    /// Re-request a contract after a protection trip, restart, or confirmed rail loss.
    /// @return true if a PDO request was started; false if recovery is delayed or fails.
    bool attemptRecovery();
    /// Set the active state and record when it was entered.
    void enter(PDNegotiationState state);
    uint8_t pollInputs(uint8_t &opMode);
    /// Reapply configuration and resume negotiation after STATUS.STARTED, which does not prove power loss.
    void handleChipRestart(uint8_t status);
    /// Start recovery for a protection trip while Ready; VOUT stays open until re-request.
    void handleProtectionTrip(uint8_t status);
    /// Confirm sustained measured-rail loss relative to the active contract.
    /// @return true after the rail stays below tolerance for the confirmation period.
    bool contractLossConfirmed();
    bool shouldRenegotiate(uint8_t status);
    bool refreshPdoTableIfDue();
    bool detectBestPdoContractChange();
    /// Disable output and return to source-capability polling for a new contract.
    void beginRenegotiation();
    /// Wait for initial source detection; Disconnected is also the initial state on boot.
    void onDisconnected(uint8_t status, uint8_t opMode);
    void onWaitingForSourceCapabilities(uint8_t status);
    /// Handle a pending PDO request until confirmed, rejected, or timed out.
    void onRequesting();
    bool contractMatchesRequest();
    void onRequestRejected();
    void onRequestAccepted();
    /// Monitor the active contract for rail loss or source-profile changes.
    void onReady(uint8_t status);

    AP33772S _pd;

    int8_t _intPin = -1;

    PDNegotiationState _state = PDNegotiationState::Disconnected;
    uint32_t _stateEnteredAt       = 0;
    uint32_t _lastStatusPollMs     = 0;
    uint32_t _lastPdoPollMs        = 0;
    uint32_t _contractLostSinceMs  = 0;
    uint32_t _lastRecoveryMs       = 0;
    uint32_t _nextRequestAllowedMs = 0;

    float   _voltage = 0.0f;
    float   _current = 0.0f;

    bool _justAttached    = false;
    bool _contractChanged = false;

    bool _shutdownLatched     = false;
    char _shutdownReason[17]  = "";
    bool _configured          = false;
    bool _configFailureLogged = false;

    // OPMODE bits 0-1 indicate a legacy CC or PD source.
    static constexpr uint8_t kOpModeSourceMask = 0x03;
    uint8_t _statusSticky = 0;
    uint8_t _lastStatus   = 0;
    uint8_t _lastOpMode   = 0xFF;
    // PD-input volts.
    float _inputVoltageV  = 0.0f;
};
