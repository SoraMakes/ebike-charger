#pragma once

#include <Arduino.h>

// Fixed RAM ring.
namespace DiagLog {

static constexpr uint8_t kCapacity = 20;
static constexpr uint8_t kDumpLines = 10;
static constexpr uint8_t kTextSize = 48;

// Store and print.
void event(const char *fmt, ...);

// Last entries, oldest first.
void dump();

}  // namespace DiagLog
