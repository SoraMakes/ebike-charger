#include "DiagLog.h"

#include <stdarg.h>
#include <string.h>

#include <esp_attr.h>
#include <esp_system.h>

namespace {

constexpr uint32_t kMagic    = 0x50444347;  // "PDCG" - marks the RTC block as valid
constexpr uint8_t  kTextSize = 40;

struct Entry {
    uint32_t ms;    ///< millis() within the boot that recorded it
    uint32_t boot;  ///< boot counter, so pre-reset events stay distinguishable
    char     text[kTextSize];
};

// RTC_NOINIT_ATTR keeps these out of .bss: they are NOT zeroed on a reset, so
// they survive a brownout, a panic or a watchdog reset. They do not survive a
// real power loss, which is what the magic word detects.
RTC_NOINIT_ATTR Entry    s_entries[DiagLog::kCapacity];
RTC_NOINIT_ATTR uint16_t s_head;
RTC_NOINIT_ATTR uint16_t s_count;
RTC_NOINIT_ATTR uint32_t s_magic;
RTC_NOINIT_ATTR uint32_t s_boot;
RTC_NOINIT_ATTR uint8_t  s_sticky;

const char *resetReasonToText(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int-wdt";
        case ESP_RST_TASK_WDT:  return "task-wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

}  // namespace

namespace DiagLog {

void begin() {
    const esp_reset_reason_t reason = esp_reset_reason();

    if (s_magic != kMagic || s_count > kCapacity || s_head >= kCapacity) {
        // Cold start (or corrupted RTC block): start a fresh log.
        s_magic = kMagic;
        s_head   = 0;
        s_count  = 0;
        s_boot   = 1;
        s_sticky = 0;
        memset(s_entries, 0, sizeof(s_entries));
    } else {
        ++s_boot;
    }

    Serial.printf("\n[DIAG] boot #%u, reset reason: %s\n", s_boot, resetReasonToText(reason));

    if (s_count > 0) {
        Serial.printf("[DIAG] %u event(s) retained from before this reset:\n", s_count);
        dump();
    }

    event("boot #%u (%s)", s_boot, resetReasonToText(reason));
}

void event(const char *fmt, ...) {
    if (s_magic != kMagic) {
        // event() before begin(): make the buffer usable rather than lose it.
        s_magic = kMagic;
        s_head   = 0;
        s_count  = 0;
        s_boot   = 1;
        s_sticky = 0;
        memset(s_entries, 0, sizeof(s_entries));
    }

    Entry &entry = s_entries[s_head];
    entry.ms   = millis();
    entry.boot = s_boot;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.text, kTextSize, fmt, args);
    va_end(args);

    s_head = static_cast<uint16_t>((s_head + 1) % kCapacity);
    if (s_count < kCapacity) {
        ++s_count;
    }

    // Also emit live, for the case where a monitor happens to be attached.
    Serial.printf("[EVT %lu.%03lus] %s\n",
                  static_cast<unsigned long>(entry.ms / 1000),
                  static_cast<unsigned long>(entry.ms % 1000),
                  entry.text);
}

void dump() {
    if (s_magic != kMagic || s_count == 0) {
        Serial.println("[DIAG] event log empty");
        return;
    }

    // s_head points one past the newest entry, so the oldest is head-count.
    uint16_t index = static_cast<uint16_t>((s_head + kCapacity - s_count) % kCapacity);
    for (uint16_t i = 0; i < s_count; ++i) {
        const Entry &entry = s_entries[index];
        Serial.printf("[DIAG] b%-2u %6lu.%03lus  %s\n",
                      entry.boot,
                      static_cast<unsigned long>(entry.ms / 1000),
                      static_cast<unsigned long>(entry.ms % 1000),
                      entry.text);
        index = static_cast<uint16_t>((index + 1) % kCapacity);
    }
}

uint32_t bootCount() {
    return s_boot;
}

const char *resetReasonText() {
    return resetReasonToText(esp_reset_reason());
}

void setStickyFlags(uint8_t flags) {
    if (s_magic != kMagic) {
        return; // begin() has not validated the RTC block yet
    }
    s_sticky = flags;
}

uint8_t stickyFlags() {
    return (s_magic == kMagic) ? s_sticky : 0;
}

}  // namespace DiagLog
