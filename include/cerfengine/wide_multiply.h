#pragma once

#include <cstdint>

namespace cerfengine {

inline void Mulu64(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi) {
    const uint32_t al = static_cast<uint32_t>(a), ah = static_cast<uint32_t>(a >> 32);
    const uint32_t bl = static_cast<uint32_t>(b), bh = static_cast<uint32_t>(b >> 32);
    const uint64_t ll  = uint64_t{al} * bl;
    const uint64_t lh  = uint64_t{al} * bh;
    const uint64_t hl  = uint64_t{ah} * bl;
    const uint64_t hh  = uint64_t{ah} * bh;
    const uint64_t mid = (ll >> 32) + static_cast<uint32_t>(lh) + static_cast<uint32_t>(hl);
    *lo = (mid << 32) | static_cast<uint32_t>(ll);
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

}
