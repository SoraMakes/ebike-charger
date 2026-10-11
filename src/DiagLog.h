#pragma once

#include <Arduino.h>

// Fixed RAM ring.
namespace DiagLog {

// Maximum number of events retained in the ring.
static constexpr uint8_t kCapacity = 20;
// Keep below the 2048-byte TX buffer: 20 timestamped lines use at most 1220 B.
static constexpr uint8_t kDumpLines = kCapacity;
// Bytes reserved for each formatted event, including its NUL terminator.
static constexpr uint8_t kTextSize = 48;

struct Entry {
    uint32_t timestampMs;
    char text[kTextSize];
};

// Store and print.
void event(const char *fmt, ...);

// Last entries, oldest first.
void dump();

// Number of entries currently retained, capped at kCapacity.
uint8_t retainedEntryCount();

// Changes for every recorded event, even when it overwrites a retained entry. "Version/Revison number"
uint32_t eventSequence();

// Copy a retained event by its zero-based, oldest-first index.
bool getEntry(uint8_t index, Entry &entry);

}  // namespace DiagLog
