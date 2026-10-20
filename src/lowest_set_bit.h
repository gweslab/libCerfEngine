#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <cerfengine/force_inline.h>

namespace cerfengine {

CERFENGINE_FORCEINLINE uint32_t LowestSetBit(uint32_t nonzero) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, nonzero);
    return static_cast<uint32_t>(index);
#else
    return static_cast<uint32_t>(__builtin_ctz(nonzero));
#endif
}

}
