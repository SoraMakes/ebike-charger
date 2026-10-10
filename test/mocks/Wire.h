#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class TwoWire {
public:
    explicit TwoWire(uint8_t) {}

    void beginTransmission(uint8_t) {}
    size_t write(uint8_t value) {
        _pendingValue = value;
        return 1;
    }
    uint8_t endTransmission() {
        writes.push_back(_pendingValue);
        return 0;
    }

    std::vector<uint8_t> writes;

private:
    uint8_t _pendingValue = 0;
};
