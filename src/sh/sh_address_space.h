#pragma once

#include <cstdint>

#include <cerfengine/unmapped_windows.h>

namespace cerfengine::ShAddressSpace {

constexpr uint32_t kP1Base               = 0x80000000u;
constexpr uint32_t kP3Base               = 0xC0000000u;
constexpr uint32_t kP4Base               = 0xE0000000u;
constexpr uint32_t kCacheAddressArray    = 0xF0000000u;
constexpr uint32_t kCacheDataArray       = 0xF1000000u;
constexpr uint32_t kP4OnChipIoBase       = 0xFC000000u;
constexpr uint32_t kArea7PaBase          = 0x1C000000u;
constexpr uint32_t kCacheEntryValidDirty = 0x00000003u;
constexpr uint32_t kCacheTagShadow       = 0xE0000000u;
constexpr uint32_t kCacheAssociative     = 0x00000008u;

constexpr bool DirectlyMapped(uint32_t va) {
    return UnmappedWindows::Contains(va) || va >= kP4OnChipIoBase;
}

constexpr bool Paged(uint32_t va) { return va < kP1Base || (va >= kP3Base && va < kP4Base); }

constexpr bool PrivilegedOnly(uint32_t va) { return va >= kP1Base; }

constexpr bool CacheAddressArray(uint32_t va) { return va >= kCacheAddressArray && va < kCacheDataArray; }

constexpr bool ReservedArea7(uint32_t va) {
    return UnmappedWindows::Contains(va) && UnmappedWindows::Pa(va) >= kArea7PaBase;
}

constexpr uint32_t Pa(uint32_t va) { return UnmappedWindows::Pa(va); }

}
