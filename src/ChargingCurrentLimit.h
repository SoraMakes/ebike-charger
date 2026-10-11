#pragma once

/// Persist and expose the selected charging-current limit.
class ChargingCurrentLimit {
public:
    /// Load the saved mode, defaulting to limited mode, and log it.
    void begin();

    /// Toggle and persist the mode.
    void toggle();

    /// Return true when limited mode is selected.
    bool isLimited() const { return _limited; }

    /// Return the selected maximum charging current.
    float maximumCurrentA() const;

private:
    bool _limited = true;
};
