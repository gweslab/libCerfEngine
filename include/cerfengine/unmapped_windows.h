#pragma once

#include <cstdint>

namespace cerfengine {

namespace UnmappedWindows {
    constexpr uint32_t kCachedBase   = 0x80000000;
    constexpr uint32_t kUncachedBase = 0xA0000000;
    constexpr uint32_t kEnd          = 0xC0000000;
    constexpr uint32_t kPaMask       = 0x1FFFFFFF;

    constexpr bool Contains(uint32_t va) {
        return va >= kCachedBase && va < kEnd;
    }
    constexpr uint32_t Pa(uint32_t va) { return va & kPaMask; }
}

}
