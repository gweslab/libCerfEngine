#pragma once

#include <array>
#include <cstddef>

#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "../cerfir_op.h"
#include "a64_host_abi.h"
#include "a64_lowering.h"

namespace cerfengine::cerfir {

using A64LowerFn = void (*)(A64Lowering& lowering, Ref ref);

extern const std::array<A64LowerFn, static_cast<size_t>(Op::kCount)> kA64LowerTable;

inline A64LowerFn A64LowerFnFor(Op op) { return kA64LowerTable[static_cast<size_t>(op)]; }

constexpr uint8_t kA64Tmp0 = a64::kX9;
constexpr uint8_t kA64Tmp1 = a64::kX10;
constexpr uint8_t kA64Tmp2 = a64::kX11;

constexpr uint8_t kA64HostReg    = a64::kX0;
constexpr uint8_t kA64AddressReg = a64::kX1;
constexpr uint8_t kA64ValueReg   = a64::kX2;

void A64LowerGetSlot(A64Lowering& lowering, Ref ref);

void A64StoreSlot(A64Lowering& l, Ref value, int32_t disp);
void A64StoreSlotSext(A64Lowering& l, Ref value, int32_t disp);

struct A64CallSite {
    const void*    fn;
    uint32_t       num_args;
    Ref            args[3];
    const CallArg* extra;
    uint32_t       num_extra;
    bool           store_pc;
    uint32_t       pc;
    Ref            result;
    Type           result_type;
};

CERFENGINE_FORCEINLINE void A64LoadCallArg(uint8_t*& c, const CallArg& a, uint8_t reg) {
    switch (a.kind) {
    case CallArgKind::kImm:
        a64::EmitMovImm32(c, reg, static_cast<uint32_t>(a.value));
        return;
    case CallArgKind::kHostAddress:
        a64::EmitMovPtr(c, reg, a.Host());
        return;
    case CallArgKind::kHostWord:
        a64::EmitMovPtr(c, a64::kIp0, a.Host());
        a64::EmitLdrW(c, reg, a64::kIp0, 0);
        return;
    }
    throw EmitFault("CerfIR A64 lowering: a call argument has no load");
}

void A64EmitCall(A64Lowering& lowering, const A64CallSite& site);
void A64LowerCall(A64Lowering& lowering, Ref ref);
void A64LowerLoad(A64Lowering& lowering, Ref ref);
void A64LowerSwap(A64Lowering& lowering, Ref ref);
void A64CallLoad(A64Lowering& lowering, Ref ref, Ref address, const MemAttr& attr);
void A64CallStore(A64Lowering& lowering, Ref address, Ref value, const MemAttr& attr);

template <Op kOp>
void A64LowerBinaryOp(A64Lowering& lowering, Ref ref);
void A64LowerCarryArith(A64Lowering& lowering, Ref ref);
void A64LowerInvertedLogic(A64Lowering& lowering, Ref ref);
void A64LowerUnary(A64Lowering& lowering, Ref ref);
void A64LowerClz(A64Lowering& lowering, Ref ref);
void A64LowerExtend(A64Lowering& lowering, Ref ref);
void A64LowerSaturate(A64Lowering& lowering, Ref ref);
void A64LowerOrSticky(A64Lowering& lowering, Ref ref);
void A64LowerSignedSat(A64Lowering& lowering, Ref ref);
void A64LowerHostLoad(A64Lowering& lowering, Ref ref);
void A64LowerHostStore(A64Lowering& lowering, Ref ref);
void A64LowerHostTest(A64Lowering& lowering, Ref ref);
void A64LowerHostReplace(A64Lowering& lowering, Ref ref);
void A64LowerSelect(A64Lowering& lowering, Ref ref);

void A64LowerShiftCarry(A64Lowering& lowering, Ref ref);
void A64LowerRrx(A64Lowering& lowering, Ref ref);
void A64LowerShiftModulo(A64Lowering& lowering, Ref ref);

void A64LowerMul(A64Lowering& lowering, Ref ref);
void A64LowerMulWide(A64Lowering& lowering, Ref ref);
void A64LowerDivRem(A64Lowering& lowering, Ref ref);
void A64LowerHalf(A64Lowering& lowering, Ref ref);
void A64LowerWideAdd(A64Lowering& lowering, Ref ref);
void A64LowerWideNeg(A64Lowering& lowering, Ref ref);
void A64LowerWideShift(A64Lowering& lowering, Ref ref);
uint8_t A64HalfReg(A64Lowering& lowering, const A64WideHalf& half, bool allow_zr);
void    A64ReleaseHalf(A64Lowering& lowering, const A64WideHalf& half, uint8_t reg);
void    A64RecordWideNz(A64Lowering& lowering, Ref ref, uint8_t lo, uint8_t hi);

void A64LowerCompare(A64Lowering& lowering, Ref ref);
void A64LowerFlag(A64Lowering& lowering, Ref ref);
void A64StoreProducerFlags(A64Lowering& lowering, Ref producer);
void A64StoreFlagsFrom(A64Lowering& lowering, uint32_t mask, const A64FlagSource& source);
void A64StoreNzcvFlags(A64Lowering& lowering, uint32_t mask, const uint8_t (&where)[4]);
void A64LowerUnsupported(A64Lowering& lowering, Ref ref);

void A64SetCarryFromValue(A64Lowering& lowering, Ref carry);

CERFENGINE_FORCEINLINE void A64RecordNz(A64Lowering& l, Ref ref) {
    A64FlagSource& f = l.FlagSource();
    if (f.producer != ref) {
        f.producer = ref;
        f.kind[2]  = A64FlagKind::kNone;
        f.kind[3]  = A64FlagKind::kNone;
    }
    f.kind[0]  = A64FlagKind::kNzcv;
    f.kind[1]  = A64FlagKind::kNzcv;
    f.where[0] = a64::kMi;
    f.where[1] = a64::kEq;
}

CERFENGINE_FORCEINLINE void A64RecordArith(A64Lowering& l, Ref ref) {
    A64FlagSource& f = l.FlagSource();
    f.producer       = ref;
    f.kind[0] = f.kind[1] = f.kind[2] = f.kind[3] = A64FlagKind::kNzcv;
    f.where[0]       = a64::kMi;
    f.where[1]       = a64::kEq;
    f.where[2]       = a64::kCs;
    f.where[3]       = a64::kVs;
}

inline constexpr uint8_t kA64ArithFlagConds[4] = {a64::kMi, a64::kEq, a64::kCs, a64::kVs};

void A64EmitSetSlot(A64Lowering& lowering, Ref value, int32_t disp, bool sext);
void A64EmitLabel(A64Lowering& lowering, uint32_t label);
void A64EmitBranch(A64Lowering& lowering, Ref cond, uint32_t label, bool if_true);
void A64EmitBranchSlots(A64Lowering& lowering, const SlotTest& test, uint32_t label);
uint8_t* A64EmitGuardBranch(A64Lowering& lowering, const SlotTest& test);
void     A64EmitSetSlotTest(A64Lowering& lowering, const SlotTest& test, int32_t dest);
void A64EmitExitDirect(A64Lowering& lowering, uint32_t target, uint16_t flags);
void A64EmitExitIndirect(A64Lowering& lowering);
void A64EmitExitDispatcher(A64Lowering& lowering);
void A64EmitFusedAlu(A64Lowering& lowering, Op op, Ref a, Ref b, uint32_t mask, int32_t dest,
                     bool sext);
void A64EmitLoad(A64Lowering& lowering, Ref ref, Ref address, Ref writeback, uint32_t attr);
void A64EmitStore(A64Lowering& lowering, Ref address, Ref value, Ref writeback, uint32_t attr);
void A64EmitTransferWords(A64Lowering& lowering, uint32_t attr, uint32_t list);

bool A64AluImmFits(Op op, uint32_t imm);
void A64EmitAlu(uint8_t*& c, Op op, uint8_t dst, uint8_t lhs, const A64RegAlloc::Operand& rhs,
                bool set_flags);
A64RegAlloc::Operand A64AluOperand(A64RegAlloc& ra, Op op, Ref r);

[[noreturn]] void A64FusedFlagFault();

CERFENGINE_FORCEINLINE void A64CheckFusedFlags(Op op, uint32_t mask) {
    if (op != Op::kAdd && op != Op::kSub && (mask & 0x0Cu) != 0u) A64FusedFlagFault();
}

}
