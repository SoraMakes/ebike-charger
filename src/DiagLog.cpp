#include "DiagLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace DiagLog {

namespace {
char s_entries[kCapacity][kTextSize];
uint32_t s_timestampsMs[kCapacity];
uint8_t s_head = 0;
uint8_t s_count = 0;
uint32_t s_eventSequence = 0;

void printEntry(const char *prefix, uint32_t timestampMs, const char *text) {
    Serial.printf("%s%lu.%01lus | %s\n", prefix,
                  static_cast<unsigned long>(timestampMs / 1000),
                  static_cast<unsigned long>((timestampMs % 1000) / 100), text);
}
}  // namespace

void event(const char *fmt, ...) {
    const uint32_t timestampMs = millis();
    char *slot = s_entries[s_head];
    va_list args;
    va_start(args, fmt);
    vsnprintf(slot, kTextSize, fmt, args);
    va_end(args);
    slot[kTextSize - 1] = '\0';
    s_timestampsMs[s_head] = timestampMs;
    s_head = static_cast<uint8_t>((s_head + 1) % kCapacity);
    if (s_count < kCapacity) {
        ++s_count;
    }
    ++s_eventSequence;
    printEntry("[EVT] ", timestampMs, slot);
}

void dump() {
    if (s_count == 0) {
        return;
    }
    uint8_t n = s_count < kDumpLines ? s_count : kDumpLines;
    uint8_t start = static_cast<uint8_t>((s_head + kCapacity - n) % kCapacity);
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t index = static_cast<uint8_t>((start + i) % kCapacity);
        printEntry("", s_timestampsMs[index], s_entries[index]);
    }
}

uint8_t retainedEntryCount() {
    return s_count;
}

uint32_t eventSequence() {
    return s_eventSequence;
}

bool getEntry(uint8_t index, Entry &entry) {
    if (index >= s_count) {
        return false;
    }

    const uint8_t oldestIndex = static_cast<uint8_t>((s_head + kCapacity - s_count) % kCapacity);
    const uint8_t ringIndex = static_cast<uint8_t>((oldestIndex + index) % kCapacity);
    entry.timestampMs = s_timestampsMs[ringIndex];
    memcpy(entry.text, s_entries[ringIndex], kTextSize);
    return true;
}

}  // namespace DiagLog
