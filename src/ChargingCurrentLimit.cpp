#include "ChargingCurrentLimit.h"

#include "DiagLog.h"
#include <Preferences.h>

namespace {
    static constexpr char kPreferencesNamespace[] = "charger";
    static constexpr char kCurrentLimitKey[] = "limited";
    static constexpr float kLimitedCurrentA = 2.0f;
    static constexpr float kUnlimitedCurrentA = 5.0f;

    const char *modeName(bool limited) {
        return limited ? "limited 2A" : "unlimited 5A";
    }
} // namespace

void ChargingCurrentLimit::begin() {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) {
        _limited = true;
        DiagLog::event("current limit boot: %s (NVS fail)", modeName(_limited));
        return;
    }

    const bool hasSavedMode = preferences.isKey(kCurrentLimitKey);
    _limited = preferences.getBool(kCurrentLimitKey, true);
    const bool modeStored = hasSavedMode || preferences.putBool(kCurrentLimitKey, _limited) != 0;
    preferences.end();

    DiagLog::event("current limit boot: %s", modeName(_limited));
    if (!modeStored) {
        DiagLog::event("current limit save failed");
    }
}

void ChargingCurrentLimit::toggle() {
    _limited = !_limited;
    Preferences preferences;
    bool modeStored = false;
    if (preferences.begin(kPreferencesNamespace, false)) {
        modeStored = preferences.putBool(kCurrentLimitKey, _limited) != 0;
        preferences.end();
    }

    DiagLog::event("current limit: %s", modeName(_limited));
    if (!modeStored) {
        DiagLog::event("current limit save failed");
    }
}

float ChargingCurrentLimit::maximumCurrentA() const {
    return _limited ? kLimitedCurrentA : kUnlimitedCurrentA;
}
