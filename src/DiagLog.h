#pragma once

#include <Arduino.h>

/// Crash-surviving diagnostic event log.
///
/// The charger can only be observed after the fact: the ESP is plugged into a
/// PC minutes after the fault, while the fault is still latched. So nothing may
/// be printed only once. This module keeps a timestamped ring buffer in RTC
/// memory (which survives a brownout/watchdog reset, though not a full power
/// loss) and re-prints it on demand.
namespace DiagLog {

/// Number of retained events. Sized to cover a full attach sequence.
static constexpr uint8_t kCapacity = 40;

/// Restores the log across a reset, bumps the boot counter and prints the
/// reset reason plus anything carried over from the previous boot.
/// Call once, first thing in setup(), after Serial.begin().
void begin();

/// Appends a timestamped event. printf-style, truncated to 47 characters.
/// Cheap enough to call from any state transition.
void event(const char *fmt, ...);

/// Re-prints the whole ring buffer, oldest first.
void dump();

/// Number of boots since the last full power-on (1 on a cold boot).
uint32_t bootCount();

/// Reset reason of the current boot as a short string ("brownout", "panic", ...).
const char *resetReasonText();

/// A byte of caller-defined flags kept in RTC memory alongside the event log,
/// so it survives a brownout/watchdog reset. PDManager uses it for the sticky
/// AP33772S STATUS bits: an ordinary member would be zeroed by the very reset
/// the fault caused, losing the evidence. Cleared on a cold power-on only.
void    setStickyFlags(uint8_t flags);
uint8_t stickyFlags();

}  // namespace DiagLog
