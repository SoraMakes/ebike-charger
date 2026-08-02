#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_HUSB238.h>

/// Negotiation state for the active USB-PD source.
enum class PDNegotiationState : uint8_t {
    Disconnected,        ///< No source attached.
    AttachSettle,        ///< Source just attached; letting CC/cable detection settle.
    WaitingCapabilities, ///< Waiting for the source's capability table.
    Requesting,          ///< A PDO was selected and requested; waiting for the contract.
    Ready,               ///< The requested (or default 5 V) contract is active.
};

/// Manages USB-PD source negotiation: detects attachment, probes the
/// available voltages from highest to lowest, requests the best one, and
/// re-negotiates automatically on reconnect or contract fallback.
class PDManager {
public:
    PDManager() = default;

    /// Initializes the USB-PD controller on the specified I2C bus.
    /// @return true when the controller is available.
    bool begin(TwoWire &wire);

    /// Advances the negotiation state machine. Call this periodically (e.g. every 100-250 ms).
    void update();

    /// Reports whether a USB-PD source is currently attached (may still be negotiating).
    bool isAttached() const;

    /// Reports whether a contract is active and ready for use.
    bool isReady() const;

    /// True once when a source becomes ready (initial contract or after re-negotiation).
    bool justAttached();

    /// True once when a previously ready source goes away.
    bool justDetached();

    /// True once when the active contract voltage changes while already ready
    /// (e.g. the source falls back to 5 V).
    bool voltageJumped();

    /// Returns the voltage of the active source contract in volts.
    float getVoltage() const;

    /// Returns the current limit of the active source contract in amps.
    float getCurrent() const;

    /// Returns the power limit of the active source contract in watts.
    float getWattage() const;

private:
    /// Requests source capabilities and (re)starts negotiation.
    void requestCapabilities();

    /// Scans available PDOs from highest to lowest voltage, logs them, and
    /// requests the first (highest) one found.
    /// @return true if a PDO above 5 V was found and requested.
    bool selectAndRequestBestPdo();

    Adafruit_HUSB238 _husb;

    PDNegotiationState _state            = PDNegotiationState::Disconnected;
    uint32_t           _stateEnteredAt   = 0;
    float              _requestedVoltage = 0.0f;
    uint8_t            _failedAttachReads = 0;

    float _voltage = 0.0f;
    float _current = 0.0f;

    bool _justAttached  = false;
    bool _justDetached  = false;
    bool _voltageJumped = false;

    static float _voltageFromActiveSetting(HUSB238_VoltageSetting vs);
    static float _ampsFromCurrentSetting(HUSB238_CurrentSetting cur);
};
