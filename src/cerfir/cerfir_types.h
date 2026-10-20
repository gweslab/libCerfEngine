#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/force_inline.h>

namespace cerfengine::cerfir {

enum class Type : uint8_t {
    kVoid,
    kI1,
    kI8,
    kI16,
    kI32,
    kI64,
    kPtr,
};

using Ref = uint16_t;

constexpr Ref      kNoRef         = 0xFFFFu;
constexpr Ref      kConstRefBit   = 0x8000u;
constexpr Ref      kSlotRefBit    = 0x4000u;
constexpr uint32_t kMaxInsts      = 0x3FFFu;
constexpr uint32_t kMaxConsts     = 0x7FFFu;
constexpr uint32_t kSlotRefBytes  = 0x400u;

CERFENGINE_FORCEINLINE constexpr bool IsConstRef(Ref r) {
    return r != kNoRef && (r & kConstRefBit) != 0u;
}
CERFENGINE_FORCEINLINE constexpr Ref ConstRef(uint16_t index) {
    return static_cast<Ref>(index | kConstRefBit);
}
CERFENGINE_FORCEINLINE constexpr uint16_t ConstIndex(Ref r) {
    return static_cast<uint16_t>(r & ~kConstRefBit);
}

CERFENGINE_FORCEINLINE constexpr bool    IsValueRef(Ref r) { return r < kSlotRefBit; }
CERFENGINE_FORCEINLINE constexpr bool    IsSlotRef(Ref r) { return (r & 0xC000u) == kSlotRefBit; }
CERFENGINE_FORCEINLINE constexpr Ref     SlotRef(uint32_t disp) { return static_cast<Ref>(kSlotRefBit | disp); }
CERFENGINE_FORCEINLINE constexpr int32_t SlotRefDisp(Ref r) { return static_cast<int32_t>(r & 0x3FFFu); }

inline constexpr uint8_t kTypeBitsTable[] = {
    0u, 1u, 8u, 16u, 32u, 64u, static_cast<uint8_t>(sizeof(void*) * 8u),
};

CERFENGINE_FORCEINLINE constexpr uint32_t TypeBits(Type t) {
    return kTypeBitsTable[static_cast<size_t>(t)];
}

CERFENGINE_FORCEINLINE constexpr uint64_t TypeMask(Type t) {
    const uint32_t bits = TypeBits(t);
    return bits >= 64u ? ~0ull : ((1ull << bits) - 1ull);
}

constexpr uint64_t SignBit(Type t) {
    const uint32_t bits = TypeBits(t);
    return bits == 0u ? 0u : (1ull << (bits - 1u));
}

}
