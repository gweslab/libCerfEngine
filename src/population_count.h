#pragma once

#include <cstdint>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#else
#include <bit>
#endif

namespace cerfengine {

inline uint32_t PopulationCount16(uint16_t value) {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    return __popcnt16(value);
#else
    return static_cast<uint32_t>(std::popcount(value));
#endif
}

}
