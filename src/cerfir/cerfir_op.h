#pragma once

#include <cstddef>
#include <cstdint>

#include "cerfir_types.h"

namespace cerfengine::cerfir {

enum OpFlag : uint32_t {
    kOpPure          = 1u << 0,
    kOpSideEffect    = 1u << 1,
    kOpReadsState    = 1u << 2,
    kOpWritesState   = 1u << 3,
    kOpMayFault      = 1u << 4,
    kOpTerminal      = 1u << 5,
    kOpControl       = 1u << 6,
    kOpFlagPseudo    = 1u << 7,
    kOpArith         = 1u << 8,
    kOpNzResult      = 1u << 9,
    kOpShifterCarry  = 1u << 10,
};

enum class Op : uint8_t {
#define CERFIR_OP(name, nargs, flags) k##name,
#include "cerfir_ops.inc"
#undef CERFIR_OP
    kCount,
};

struct OpInfo {
    const char* name;
    uint8_t     num_args;
    uint32_t    flags;
};

inline constexpr OpInfo kOpInfoTable[] = {
#define CERFIR_OP(name, nargs, flags) OpInfo{#name, nargs, flags},
#include "cerfir_ops.inc"
#undef CERFIR_OP
};

static_assert(sizeof(kOpInfoTable) / sizeof(kOpInfoTable[0]) == static_cast<size_t>(Op::kCount),
              "the op table and the op enum come from one list");

CERFENGINE_FORCEINLINE const OpInfo& InfoOf(Op op) { return kOpInfoTable[static_cast<size_t>(op)]; }

constexpr uint8_t SlotArgMaskOf(Op op) {
    switch (op) {
    case Op::kAdd: case Op::kSub: case Op::kAdc: case Op::kSbc: case Op::kAnd: case Op::kOr:
    case Op::kXor: case Op::kAndNot: case Op::kOrNot: case Op::kShl: case Op::kLsr:
    case Op::kAsr: case Op::kRor: case Op::kShlC: case Op::kLsrC: case Op::kAsrC:
    case Op::kRorC: case Op::kPair: case Op::kCmpEq: case Op::kCmpNe: case Op::kCmpLtS:
    case Op::kCmpLtU: case Op::kCmpLeS:
        return 3u;
    case Op::kNot: case Op::kNeg: case Op::kCopy: case Op::kByteSwap: case Op::kRrx:
    case Op::kSetSlot:
        return 1u;
    case Op::kStore:
        return 2u;
    default:
        return 0u;
    }
}

inline constexpr uint8_t kSlotArgMask[] = {
#define CERFIR_OP(name, nargs, flags) SlotArgMaskOf(Op::k##name),
#include "cerfir_ops.inc"
#undef CERFIR_OP
};

CERFENGINE_FORCEINLINE bool OpHas(Op op, uint32_t flag) { return (InfoOf(op).flags & flag) != 0u; }

inline bool FlagDefinedFor(Op flag, Op producer) {
    switch (flag) {
    case Op::kNFlag:
    case Op::kZFlag:
        return OpHas(producer, kOpArith | kOpNzResult);
    case Op::kCFlag:
        return OpHas(producer, kOpArith | kOpShifterCarry);
    case Op::kVFlag:
        return OpHas(producer, kOpArith);
    default:
        return false;
    }
}

constexpr uint16_t kNoDest = 0xFFFFu;

struct Inst {
    Op       op;
    Type     type;
    Ref      args[3];
    uint16_t aux16;
    uint16_t dest;
    uint32_t aux;
};

static_assert(sizeof(Inst) == 16, "a CerfIR instruction is 16 bytes");

}
