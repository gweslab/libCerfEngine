#pragma once

#include <cstddef>
#include <cstdint>

class StateDigest {
public:
    void AddBytes(const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) AddByte(data[i]);
    }

    void Add32(uint32_t value) {
        for (uint32_t shift = 0; shift < 32u; shift += 8u) {
            AddByte(static_cast<uint8_t>(value >> shift));
        }
    }

    void Add64(uint64_t value) {
        Add32(static_cast<uint32_t>(value));
        Add32(static_cast<uint32_t>(value >> 32));
    }

    unsigned long long Value() const { return value_; }

private:
    void AddByte(uint8_t byte) {
        value_ ^= byte;
        value_ *= 0x100000001B3ull;
    }

    uint64_t value_ = 0xCBF29CE484222325ull;
};
