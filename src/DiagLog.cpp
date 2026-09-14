#include "DiagLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace DiagLog {

namespace {
char s_entries[kCapacity][kTextSize];
uint8_t s_head = 0;
uint8_t s_count = 0;
}  // namespace

void event(const char *fmt, ...) {
    char *slot = s_entries[s_head];
    va_list args;
    va_start(args, fmt);
    vsnprintf(slot, kTextSize, fmt, args);
    va_end(args);
    slot[kTextSize - 1] = '\0';
    s_head = static_cast<uint8_t>((s_head + 1) % kCapacity);
    if (s_count < kCapacity) {
        ++s_count;
    }
    Serial.printf("[EVT] %s\n", slot);
}

void dump() {
    if (s_count == 0) {
        return;
    }
    uint8_t n = s_count < kDumpLines ? s_count : kDumpLines;
    uint8_t start = static_cast<uint8_t>((s_head + kCapacity - n) % kCapacity);
    for (uint8_t i = 0; i < n; ++i) {
        Serial.printf("[LOG] %s\n", s_entries[(start + i) % kCapacity]);
    }
}

}  // namespace DiagLog
