#pragma once

#include <Arduino.h>

// Minimal event log: fixed RAM ring, short lines, no RTC.
namespace DiagLog {

static constexpr uint8_t kCapacity = 20;
static constexpr uint8_t kDumpLines = 10;
static constexpr uint8_t kTextSize = 48;

// Store event and print it immediately.
void event(const char *fmt, ...);

// Print last kDumpLines entries, oldest first.
void dump();

}  // namespace DiagLog
