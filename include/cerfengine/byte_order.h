#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

static_assert(std::endian::native == std::endian::little);

namespace cerfengine {

inline uint32_t ByteWidthMask(size_t bytes) {
    return bytes >= 4 ? 0xFFFFFFFFu : ((1u << (bytes * 8u)) - 1u);
}

}

namespace cerfengine::le {

inline uint16_t U16(const uint8_t* p, size_t off = 0) {
    uint16_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

inline uint32_t U32(const uint8_t* p, size_t off = 0) {
    uint32_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

inline uint64_t U64(const uint8_t* p, size_t off = 0) {
    uint64_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

inline uint32_t U24(const uint8_t* p, size_t off = 0) {
    return uint32_t(p[off]) | (uint32_t(p[off + 1]) << 8) | (uint32_t(p[off + 2]) << 16);
}

inline uint64_t UN(const uint8_t* p, size_t width) {
    switch (width) {
        case 1: return p[0];
        case 2: return U16(p);
        case 3: return U24(p);
        case 4: return U32(p);
        default: {
            uint64_t v = 0;
            std::memcpy(&v, p, width);
            return v;
        }
    }
}

inline void Put16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, sizeof(v)); }
inline void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
inline void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

inline void Put24(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
}

inline void PutN(uint8_t* p, uint64_t v, size_t width) {
    switch (width) {
        case 1: p[0] = uint8_t(v); return;
        case 2: Put16(p, uint16_t(v)); return;
        case 3: Put24(p, uint32_t(v)); return;
        case 4: Put32(p, uint32_t(v)); return;
        default: std::memcpy(p, &v, width); return;
    }
}

}
