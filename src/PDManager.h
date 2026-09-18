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

    // INA238 PD-input voltage (high-voltage VBUS, not 5V aux).
    void setMeasuredInputVoltage(float volts);
    bool isReady() const;
    bool justAttached() const;
    // Contract voltage changed while Ready.
    bool voltageJumped() const;
    float getVoltage() const;
    float getCurrent() const;
    void shutdown();
    bool isShutdownLatched() const { return _shutdownLatched; }

    PDNegotiationState getState() const { return _state; }
    uint8_t getStickyStatus() const { return _statusSticky; }
    uint8_t getOpMode() { return _pd.readOpMode(); }
    uint8_t getMsgResult() { return static_cast<uint8_t>(_pd.readMsgResult() & 0x07); }
    const char *getFaultText() const;
    bool hasChipFault() const;

private:
    static constexpr float kMaxRequestVoltageV = 28.0f;
    static constexpr float kMinUsablePdoCurrentA = 1.25f;
    // INA below this is sensor failure, not dead input.
    static constexpr float kInputValidMinV = 3.0f;
    static constexpr uint32_t kAttachSettlePollMs = 150;
    // TODO: evaluate NEWPDO-only Ready recheck, drop 500ms timer if interrupt suffices.

    bool requestBestPdo();
    uint8_t findBestFixedPdo() const;
    void applyStartupConfig();
    uint8_t pollStatus();
    bool attemptRecovery(const char *reason);

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
    bool _voltageJumped = false;

    bool _shutdownLatched = false;
    bool _configured      = false;

    // Bit0=CC legacy, bit1=PD source (AP33772S OPMODE).
    static constexpr uint8_t kOpModeSourceMask = 0x03;
    // Granted VREQ must be within 0.5V of requested PDO.
    static constexpr float kVoltageMatchV = 0.5f;
    uint8_t _statusSticky = 0;
    uint8_t _lastStatus = 0;
    uint8_t _lastOpMode = 0xFF;
    // INA238 PD-input volts; fail-open when invalid.
    float _inputVoltageV = 0.0f;
    bool _inputValid = false;
};

