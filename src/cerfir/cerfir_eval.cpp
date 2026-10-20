#include "cerfir_eval.h"

#include <bit>
#include <cstdint>

namespace cerfengine::cerfir {

namespace {

struct AddResult {
    uint64_t value;
    bool     carry;
    bool     overflow;
};

/* ARM DDI 0406C.c A2.2.1 AddWithCarry() (p. A2-43). */
AddResult AddWithCarry(Type t, uint64_t x, uint64_t y, uint64_t carry_in) {
    const uint32_t bits = TypeBits(t);
    const uint64_t mask = TypeMask(t);
    x &= mask;
    y &= mask;
    carry_in &= 1u;
    uint64_t sum   = 0;
    bool     carry = false;
    if (bits >= 64u) {
        const uint64_t partial = x + y;
        sum   = partial + carry_in;
        carry = partial < x || sum < partial;
    } else {
        const uint64_t wide = x + y + carry_in;
        sum   = wide & mask;
        carry = (wide >> bits) != 0u;
    }
    const bool overflow = ((x ^ sum) & (y ^ sum) & SignBit(t)) != 0u;
    return {sum, carry, overflow};
}

bool ArithOperands(Op op, const uint64_t* v, uint64_t* y, uint64_t* carry_in) {
    switch (op) {
    case Op::kAdd: *y = v[1];  *carry_in = 0u;   return true;
    case Op::kSub: *y = ~v[1]; *carry_in = 1u;   return true;
    case Op::kAdc: *y = v[1];  *carry_in = v[2]; return true;
    case Op::kSbc: *y = ~v[1]; *carry_in = v[2]; return true;
    default: return false;
    }
}

struct ShiftResult {
    uint32_t value;
    bool     carry;
};

/* ARM DDI 0406C.c A8.4.3 Shift_C() (p. A8-292) over A2.2.1 LSL_C, LSR_C,
   ASR_C, ROR_C and RRX_C (pp. A2-41 to A2-43). */
ShiftResult ShiftC(Op op, uint32_t x, uint32_t amount, bool carry_in) {
    if (op == Op::kRrx) {
        return {(static_cast<uint32_t>(carry_in) << 31) | (x >> 1), (x & 1u) != 0u};
    }
    if (amount == 0u) return {x, carry_in};
    switch (op) {
    case Op::kShlC:
        if (amount < 32u) return {x << amount, ((x >> (32u - amount)) & 1u) != 0u};
        if (amount == 32u) return {0u, (x & 1u) != 0u};
        return {0u, false};
    case Op::kLsrC:
        if (amount < 32u) return {x >> amount, ((x >> (amount - 1u)) & 1u) != 0u};
        if (amount == 32u) return {0u, (x >> 31) != 0u};
        return {0u, false};
    case Op::kAsrC:
        if (amount < 32u) {
            return {static_cast<uint32_t>(static_cast<int32_t>(x) >> amount),
                    ((x >> (amount - 1u)) & 1u) != 0u};
        }
        return {(x >> 31) != 0u ? 0xFFFFFFFFu : 0u, (x >> 31) != 0u};
    default: {
        const uint32_t m = amount & 31u;
        const uint32_t r = m == 0u ? x : ((x >> m) | (x << (32u - m)));
        return {r, (r >> 31) != 0u};
    }
    }
}

uint32_t ByteSwap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
}

uint64_t SignExtendFrom(uint64_t value, Type from) {
    const uint64_t sign = SignBit(from);
    value &= TypeMask(from);
    return (value ^ sign) - sign;
}

int64_t SignedValue(uint64_t value, Type type) {
    return static_cast<int64_t>(SignExtendFrom(value, type));
}

}

bool EvalPure(Op op, Type t, const EvalArgs& a, uint64_t* out) {
    uint64_t v[3];
    for (int i = 0; i < 3; ++i) v[i] = a.value[i] & TypeMask(a.type[i]);
    const uint64_t mask = TypeMask(t);
    const uint32_t x32  = static_cast<uint32_t>(v[0]);
    const uint32_t y32  = static_cast<uint32_t>(v[1]);

    uint64_t y = 0, carry_in = 0;
    if (ArithOperands(op, v, &y, &carry_in)) {
        *out = AddWithCarry(t, v[0], y, carry_in).value;
        return true;
    }
    switch (op) {
    case Op::kAnd:    *out = (v[0] & v[1]) & mask;  return true;
    case Op::kOr:     *out = (v[0] | v[1]) & mask;  return true;
    case Op::kXor:    *out = (v[0] ^ v[1]) & mask;  return true;
    case Op::kAndNot: *out = (v[0] & ~v[1]) & mask; return true;
    case Op::kOrNot:  *out = (v[0] | ~v[1]) & mask; return true;
    case Op::kNot:    *out = ~v[0] & mask;          return true;
    case Op::kCopy:   *out = v[0] & mask;           return true;
    case Op::kNeg:    *out = (0u - v[0]) & mask;    return true;
    case Op::kMul:    *out = (v[0] * v[1]) & mask;  return true;
    case Op::kMulU64:
        *out = static_cast<uint64_t>(x32) * static_cast<uint64_t>(y32);
        return true;
    case Op::kMulS64:
        *out = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(x32)) *
                                     static_cast<int64_t>(static_cast<int32_t>(y32)));
        return true;
    case Op::kDivRemU32: {
        const uint32_t d = y32 == 0u ? 1u : y32;
        *out = static_cast<uint64_t>(x32 / d) | (static_cast<uint64_t>(x32 % d) << 32);
        return true;
    }
    case Op::kDivRemS32: {
        const int32_t n = static_cast<int32_t>(x32);
        int32_t       d = static_cast<int32_t>(y32);
        if (d == 0 || (n == INT32_MIN && d == -1)) d = 1;
        *out = static_cast<uint64_t>(static_cast<uint32_t>(n / d)) |
               (static_cast<uint64_t>(static_cast<uint32_t>(n % d)) << 32);
        return true;
    }
    case Op::kPair:   *out = static_cast<uint64_t>(x32) | (static_cast<uint64_t>(y32) << 32);
        return true;
    case Op::kLo32:   *out = v[0] & 0xFFFFFFFFu; return true;
    case Op::kHi32:   *out = v[0] >> 32;         return true;
    case Op::kCmpEq:  *out = v[0] == v[1] ? 1u : 0u; return true;
    case Op::kCmpNe:  *out = v[0] != v[1] ? 1u : 0u; return true;
    case Op::kCmpLtS: *out = SignedValue(v[0], a.type[0]) < SignedValue(v[1], a.type[1]) ? 1u : 0u;
        return true;
    case Op::kCmpLtU: *out = v[0] < v[1] ? 1u : 0u; return true;
    case Op::kCmpLeS: *out = SignedValue(v[0], a.type[0]) <= SignedValue(v[1], a.type[1]) ? 1u : 0u;
        return true;
    case Op::kSelect: *out = ((v[0] & 1u) != 0u ? v[1] : v[2]) & mask; return true;
    case Op::kZeroExtend: *out = v[0] & mask; return true;
    case Op::kSignExtend: *out = SignExtendFrom(v[0], a.type[0]) & mask; return true;
    case Op::kTruncate:   *out = v[0] & mask; return true;
    default: break;
    }
    if (t != Type::kI32) return false;
    switch (op) {
    case Op::kShl: *out = x32 << (y32 & 31u); return true;
    case Op::kLsr: *out = x32 >> (y32 & 31u); return true;
    case Op::kAsr:
        *out = static_cast<uint32_t>(static_cast<int32_t>(x32) >> (y32 & 31u));
        return true;
    case Op::kRor: {
        const uint32_t m = y32 & 31u;
        *out = m == 0u ? x32 : ((x32 >> m) | (x32 << (32u - m)));
        return true;
    }
    case Op::kShlC:
    case Op::kLsrC:
    case Op::kAsrC:
    case Op::kRorC:
        *out = ShiftC(op, x32, y32, (v[2] & 1u) != 0u).value;
        return true;
    case Op::kRrx:
        *out = ShiftC(op, x32, 1u, (v[1] & 1u) != 0u).value;
        return true;
    case Op::kClz:      *out = static_cast<uint32_t>(std::countl_zero(x32)); return true;
    case Op::kByteSwap: *out = ByteSwap32(x32); return true;
    default: return false;
    }
}

bool EvalFlag(Op flag, Op producer, Type producer_type, const EvalArgs& a, uint64_t* out) {
    if (!FlagDefinedFor(flag, producer)) return false;
    uint64_t v[3];
    for (int i = 0; i < 3; ++i) v[i] = a.value[i] & TypeMask(a.type[i]);

    uint64_t y = 0, carry_in = 0;
    if (ArithOperands(producer, v, &y, &carry_in)) {
        const AddResult r = AddWithCarry(producer_type, v[0], y, carry_in);
        switch (flag) {
        case Op::kNFlag: *out = (r.value & SignBit(producer_type)) != 0u ? 1u : 0u; return true;
        case Op::kZFlag: *out = r.value == 0u ? 1u : 0u; return true;
        case Op::kCFlag: *out = r.carry ? 1u : 0u;       return true;
        case Op::kVFlag: *out = r.overflow ? 1u : 0u;    return true;
        default: return false;
        }
    }
    if (flag == Op::kCFlag) {
        const ShiftResult s = producer == Op::kRrx
            ? ShiftC(producer, static_cast<uint32_t>(v[0]), 1u, (v[1] & 1u) != 0u)
            : ShiftC(producer, static_cast<uint32_t>(v[0]), static_cast<uint32_t>(v[1]),
                     (v[2] & 1u) != 0u);
        *out = s.carry ? 1u : 0u;
        return true;
    }
    uint64_t result = 0;
    if (!EvalPure(producer, producer_type, a, &result)) return false;
    *out = flag == Op::kNFlag ? ((result & SignBit(producer_type)) != 0u ? 1u : 0u)
                              : (result == 0u ? 1u : 0u);
    return true;
}

}
