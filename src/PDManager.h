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

    /// Permanently switches off the RotoPD Pro's VBUS switch and prevents
    /// further re-negotiation. Idempotent — safe to call multiple times.
    void shutdown();

private:
    /// Highest fixed-PDO voltage we are willing to request.
    static constexpr float kMaxRequestVoltageV = 28.0f;

    /// Requests the best eligible fixed PDO found in the cached table.
    /// @return true if a request was sent.
    bool requestBestPdo();

    /// Scans the cached PDO table for the highest fixed PDO <= kMaxRequestVoltageV.
    /// @return the 1-based PDO number (1..13) or 0 if none found.
    uint8_t findBestFixedPdo() const;

    /// Checks if a power source is physically attached via OPMODE / VBUS voltage.
    bool isSourcePhysicallyPresent();

    AP33772S _pd;

    int8_t _intPin = -1;

    PDNegotiationState _state          = PDNegotiationState::Disconnected;
    uint32_t           _stateEnteredAt = 0;

    uint8_t _activePdoIndex  = 0; // 1-based PDO number, 0 = none
    float   _voltage = 0.0f;
    float   _current = 0.0f;

    bool _justAttached  = false;
    bool _justDetached  = false;
    bool _voltageJumped = false;

    bool _shutdownLatched = false;
};

