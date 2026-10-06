#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <AP33772S.h>

enum class PDNegotiationState : uint8_t {
    Disconnected,
    AttachSettle,
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
    bool isReady() const;
    bool justAttached() const;
    // True on an update pass that starts contract renegotiation or recovery.
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
    const char *getFaultText() const;
    bool hasChipFault() const;

private:
    // Minimum PD voltage supported by the boost converter.
    static constexpr float kMinChargeVoltageV = 8.5f;
    // Upper voltage limit requested from a source.
    static constexpr float kMaxRequestVoltageV = 28.0f;
    // PDOs below this advertised current are not usable by the charger.
    static constexpr float kMinUsablePdoCurrentA = 1.25f;
    // Relative rail tolerance for request validation and Ready-state monitoring.
    static constexpr float kVoltageToleranceFraction = 0.10f;
    // PDO-table refresh interval while waiting for attachment updates.
    static constexpr uint32_t kAttachSettlePollMs = 150;

    bool requestBestPdo();
    uint8_t findBestFixedPdo() const;
    void applyStartupConfig();
    uint8_t pollStatus();
    bool attemptRecovery();
    void enter(PDNegotiationState state);
    uint8_t pollInputs(uint8_t &opMode);
    void handleChipRestart(uint8_t status);
    void handleProtectionTrip(uint8_t status);
    bool contractLossConfirmed();
    bool shouldRenegotiate(uint8_t status);
    void beginRenegotiation();
    void onDisconnected(uint8_t status, uint8_t opMode);
    void onAttachSettle(uint8_t status);
    void onRequesting();
    void onReady(uint8_t status);

    AP33772S _pd;

    int8_t _intPin = -1;

    PDNegotiationState _state = PDNegotiationState::Disconnected;
    uint32_t _stateEnteredAt = 0;
    uint32_t _lastStatusPollMs = 0;
    uint32_t _lastPdoPollMs = 0;
    uint32_t _contractLostSinceMs = 0;
    uint32_t _lastRecoveryMs = 0;
    uint32_t _nextRequestAllowedMs = 0;

    float   _voltage = 0.0f;
    float   _current = 0.0f;

    bool _justAttached  = false;
    bool _contractChanged = false;

    bool _shutdownLatched = false;
    char _shutdownReason[17] = "";
    bool _configured      = false;

    // OPMODE bits 0-1 indicate a legacy CC or PD source.
    static constexpr uint8_t kOpModeSourceMask = 0x03;
    uint8_t _statusSticky = 0;
    uint8_t _lastStatus = 0;
    uint8_t _lastOpMode = 0xFF;
    // PD-input volts.
    float _inputVoltageV = 0.0f;
};
