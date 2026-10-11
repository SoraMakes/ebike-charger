#pragma once

#include <cstddef>
#include <cstring>

class Preferences {
public:
    struct State {
        bool beginSucceeds = true;
        bool hasSavedMode = false;
        bool limited = true;
        bool writeSucceeds = true;
    };

    static State &state() {
        static State preferencesState;
        return preferencesState;
    }

    static void reset() {
        state() = State{};
    }

    bool begin(const char *, bool) {
        _opened = state().beginSucceeds;
        return _opened;
    }

    bool isKey(const char *key) const {
        return _opened && std::strcmp(key, "limited") == 0 && state().hasSavedMode;
    }

    bool getBool(const char *key, bool defaultValue = false) const {
        return isKey(key) ? state().limited : defaultValue;
    }

    size_t putBool(const char *key, bool value) {
        if (!_opened || !state().writeSucceeds || std::strcmp(key, "limited") != 0) {
            return 0;
        }

        state().hasSavedMode = true;
        state().limited = value;
        return sizeof(value);
    }

    void end() {
        _opened = false;
    }

private:
    bool _opened = false;
};
