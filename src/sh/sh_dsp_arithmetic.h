#pragma once

#include <cstdint>

namespace cerfengine {

constexpr uint64_t kShDsp40Bits   = 0xFFFFFFFFFFull;
constexpr uint64_t kShDsp40Sign   = 0x8000000000ull;
constexpr int64_t  kShDspFixedMax = 0x7FFFFFFFll;
constexpr int64_t  kShDspFixedMin = -0x80000000ll;
constexpr int64_t  kShDspLowWord  = 0xFFFFll;
constexpr int64_t  kShDspOneWord  = 0x10000ll;
constexpr int64_t  kShDspRound    = 0x8000ll;
constexpr uint32_t kShDspWordMask = 0xFFFFu;

constexpr int64_t ShDspSext40(uint64_t v) {
    return static_cast<int64_t>((v & kShDsp40Bits) ^ kShDsp40Sign) - static_cast<int64_t>(kShDsp40Sign);
}

constexpr bool     ShDspFits32(int64_t v) { return v >= kShDspFixedMin && v <= kShDspFixedMax; }
constexpr uint64_t ShDspLow32(int64_t v) { return static_cast<uint32_t>(v); }
constexpr int64_t  ShDspClearLowWord(int64_t v) { return v & ~kShDspLowWord; }
constexpr uint32_t ShDspTopWord(uint32_t v) { return v >> 16; }

struct ShDspResult {
    int64_t value;
    bool    carry;
    bool    over_range;
    bool    overflow;
};

constexpr ShDspResult ShDspWrap(int64_t exact, bool carry) {
    const int64_t value = ShDspSext40(static_cast<uint64_t>(exact));
    const bool    range = value != exact;
    return {value, carry, range, range || !ShDspFits32(value)};
}

constexpr ShDspResult ShDspAdd(int64_t a, int64_t b, uint32_t carry_in) {
    return ShDspWrap(a + b + carry_in, ((ShDspLow32(a) + ShDspLow32(b) + carry_in) >> 32) != 0u);
}

constexpr ShDspResult ShDspSub(int64_t a, int64_t b, uint32_t borrow_in) {
    return ShDspWrap(a - b - borrow_in, ShDspLow32(a) < ShDspLow32(b) + borrow_in);
}

constexpr ShDspResult ShDspSaturate(ShDspResult r, bool enabled) {
    if (!enabled || !r.overflow) return r;
    r.value      = r.value < 0 ? kShDspFixedMin : kShDspFixedMax;
    r.over_range = false;
    r.overflow   = false;
    return r;
}

constexpr ShDspResult ShDspShiftArithmetic(int64_t source, int32_t amount) {
    if (amount >= 0) {
        const int64_t value = ShDspSext40(static_cast<uint64_t>(source) << amount);
        return {value, ((static_cast<uint64_t>(value) >> 32) & 1u) != 0u, false, !ShDspFits32(value)};
    }
    const int32_t n     = -amount;
    const int64_t value = source >> n;
    return {value, ((static_cast<uint64_t>(source) >> (n - 1)) & 1u) != 0u, false, !ShDspFits32(value)};
}

struct ShDspWordResult {
    uint32_t word;
    bool     carry;
};

constexpr ShDspWordResult ShDspShiftLogical(uint32_t word, int32_t amount) {
    if (amount >= 0) {
        return {(word << amount) & kShDspWordMask, amount != 0 && ((word >> (16 - amount)) & 1u) != 0u};
    }
    const int32_t n = -amount;
    return {word >> n, ((word >> (n - 1)) & 1u) != 0u};
}

constexpr int32_t ShDspMsbPosition(int64_t value) {
    const bool sign = value < 0;
    int32_t    bit  = 38;
    while (bit >= 0 && (((static_cast<uint64_t>(value) >> bit) & 1u) != 0u) == sign) --bit;
    return 30 - bit;
}

constexpr int64_t ShDspMultiply(uint32_t se, uint32_t sf, bool saturate) {
    const int32_t product = static_cast<int16_t>(ShDspTopWord(se)) * static_cast<int16_t>(ShDspTopWord(sf));
    const int32_t doubled = static_cast<int32_t>(static_cast<uint32_t>(product) << 1);
    return saturate && doubled == static_cast<int32_t>(kShDspFixedMin) ? kShDspFixedMax : doubled;
}

constexpr int32_t ShDspSignedField(uint32_t value, uint32_t bits) {
    const uint32_t sign = 1u << (bits - 1u);
    return static_cast<int32_t>((value & ((sign << 1) - 1u)) ^ sign) - static_cast<int32_t>(sign);
}

}
