#pragma once

#include <Arduino.h>

// Fixed RAM ring.
namespace DiagLog {

// Maximum number of events retained in the ring.
static constexpr uint8_t kCapacity = 20;
// Keep below the 2048-byte TX buffer: 20 lines * (47 event chars + 6-byte "[LOG] " prefix + newline) = 1080 B.
static constexpr uint8_t kDumpLines = kCapacity;
// Bytes reserved for each formatted event, including its NUL terminator.
static constexpr uint8_t kTextSize = 48;

// Store and print.
void event(const char *fmt, ...);

// Last entries, oldest first.
void dump();

}  // namespace DiagLog
