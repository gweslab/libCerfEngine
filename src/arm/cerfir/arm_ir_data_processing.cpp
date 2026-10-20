#include <array>
#include <cstdio>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

#include "../../emit_fault.h"
#include <cerfengine/force_inline.h>
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::FlagSet;
using cerfir::Op;
using cerfir::Ref;
using cerfir::kNoRef;

struct DpOperand2 {
    Ref value;
    Ref carry;
};

constexpr uint8_t kDpSwap    = 1u << 0;
constexpr uint8_t kDpCarryIn = 1u << 1;
constexpr uint8_t kDpArith   = 1u << 2;
constexpr uint8_t kDpTest    = 1u << 3;
constexpr uint8_t kDpMove    = 1u << 4;

struct DpInfo {
    Op      op;
    uint8_t flags;
};

constexpr std::array<DpInfo, kDpOrn + 1u> kDpInfo = {{
    {Op::kAnd, 0u},
    {Op::kXor, 0u},
    {Op::kSub, kDpArith},
    {Op::kSub, kDpArith | kDpSwap},
    {Op::kAdd, kDpArith},
    {Op::kAdc, kDpArith | kDpCarryIn},
    {Op::kSbc, kDpArith | kDpCarryIn},
    {Op::kSbc, kDpArith | kDpCarryIn | kDpSwap},
    {Op::kAnd, kDpTest},
    {Op::kXor, kDpTest},
    {Op::kSub, kDpArith | kDpTest},
    {Op::kAdd, kDpArith | kDpTest},
    {Op::kOr, 0u},
    {Op::kCopy, kDpMove},
    {Op::kAndNot, 0u},
    {Op::kNot, kDpMove},
    {Op::kOrNot, 0u},
}};

[[noreturn]] CERFENGINE_NOINLINE void DpOpcodeFault(uint32_t opcode) {
    char text[80];
    std::snprintf(text, sizeof(text), "CerfIR ARM frontend: data-processing opcode %u", opcode);
    throw EmitFault(text);
}

CERFENGINE_FORCEINLINE void CheckDpOpcode(const DecodedInsn& d) {
    if (d.op1 >= kDpInfo.size()) DpOpcodeFault(d.op1);
}

CERFENGINE_FORCEINLINE bool IsArith(uint32_t opcode) {
    return (kDpInfo[opcode].flags & kDpArith) != 0u;
}

CERFENGINE_FORCEINLINE bool IsTest(uint32_t opcode) {
    return (kDpInfo[opcode].flags & kDpTest) != 0u;
}

CERFENGINE_FORCEINLINE bool IsMove(uint32_t opcode) {
    return (kDpInfo[opcode].flags & kDpMove) != 0u;
}

Op ShiftCarryOp(uint32_t type) {
    switch (type) {
    case kSrLsl: return Op::kShlC;
    case kSrLsr: return Op::kLsrC;
    case kSrAsr: return Op::kAsrC;
    default:     return Op::kRorC;
    }
}

Op ShiftOp(uint32_t type) {
    switch (type) {
    case kSrLsl: return Op::kShl;
    case kSrLsr: return Op::kLsr;
    case kSrAsr: return Op::kAsr;
    default:     return Op::kRor;
    }
}

bool WritesPc(const DecodedInsn& d) {
    return !IsTest(d.op1) && d.rd == ArmGpr::kR15;
}

uint8_t ShifterCarryWant(const DecodedInsn& d) {
    return (d.s != 0u && !IsArith(d.op1) && !WritesPc(d)) ? static_cast<uint8_t>(cerfir::kWantC)
                                                          : uint8_t{0};
}

void GuardExceptionReturn(ArmIrContext& c) {
    if (WritesPc(*c.d) && c.d->s != 0u) ArmIrSpsrModeGuard(c);
}

CERFENGINE_NOINLINE void ApplyDpToPc(ArmIrContext& c, const DpInfo& info, Ref a, Ref b_arg,
                                     Ref carry_in) {
    const Ref r = c.b->Compute(info.op, a, b_arg, carry_in, 0u, nullptr);
    if (c.d->s != 0u) {
        ArmIrExceptionReturn(c, r);
    } else {
        ArmIrAluWritePc(c, r);
    }
}

CERFENGINE_FORCEINLINE uint8_t DpStoreMask(const DpInfo& info, bool s) {
    return !s ? uint8_t{0}
           : (info.flags & kDpArith) != 0u
               ? static_cast<uint8_t>(cerfir::kWantN | cerfir::kWantZ | cerfir::kWantC |
                                      cerfir::kWantV)
               : static_cast<uint8_t>(cerfir::kWantN | cerfir::kWantZ);
}

CERFENGINE_FORCEINLINE Ref ReadRn(ArmIrContext& c) {
    return IsMove(c.d->op1) ? kNoRef : ArmIrReadGpr(c, c.d->rn);
}

CERFENGINE_FORCEINLINE void ApplyDp(ArmIrContext& c, const DpOperand2& op2) {
    const DecodedInsn& d     = *c.d;
    cerfir::Builder&   b     = *c.b;
    const DpInfo&      info  = kDpInfo[d.op1];
    const bool         s     = d.s != 0u;
    constexpr uint8_t  kGeneric = kDpSwap | kDpMove | kDpCarryIn;
    const bool         test     = (info.flags & kDpTest) != 0u;
    if ((info.flags & kGeneric) == 0u && d.rn != ArmGpr::kR15 &&
        (test ? s : d.rd != ArmGpr::kR15)) {
        b.ComputeSlotInto(info.op, static_cast<uint32_t>(ArmGprDisp(d.rn)), op2.value,
                          DpStoreMask(info, s),
                          test ? cerfir::kNoDest : static_cast<uint32_t>(ArmGprDisp(d.rd)));
        if (s && (info.flags & kDpArith) == 0u && op2.carry != kNoRef) {
            ArmIrWriteFlag(c, ArmIrFlag::kC, op2.carry);
        }
        return;
    }
    const Ref          rn    = ReadRn(c);
    const Ref          a     = (info.flags & (kDpSwap | kDpMove)) != 0u ? op2.value : rn;
    const Ref          b_arg = (info.flags & kDpMove) != 0u ? kNoRef
                               : (info.flags & kDpSwap) != 0u ? rn : op2.value;
    const Ref          cin   = (info.flags & kDpCarryIn) != 0u ? ArmIrReadFlag(c, ArmIrFlag::kC)
                                                               : kNoRef;
    if ((info.flags & kDpTest) == 0u && d.rd == ArmGpr::kR15) {
        ApplyDpToPc(c, info, a, b_arg, cin);
        return;
    }
    const uint8_t store = DpStoreMask(info, s);
    if ((info.flags & kDpTest) != 0u) {
        b.ComputeStoreFlags(info.op, a, b_arg, cin, store);
    } else {
        b.ComputeInto(info.op, a, b_arg, cin, store, static_cast<uint32_t>(ArmGprDisp(d.rd)));
    }
    if (s && (info.flags & kDpArith) == 0u && op2.carry != kNoRef) {
        ArmIrWriteFlag(c, ArmIrFlag::kC, op2.carry);
    }
}

CERFENGINE_FORCEINLINE bool IsPlainMove(const DecodedInsn& d) {
    return d.op1 == 13u && d.s == 0u && d.rd != ArmGpr::kR15;
}

CERFENGINE_FORCEINLINE bool IsFusableDp(const DpInfo& info) {
    const bool fusable = info.op == Op::kAdd || info.op == Op::kSub || info.op == Op::kAnd ||
                         info.op == Op::kOr || info.op == Op::kXor;
    return fusable && (info.flags & (kDpSwap | kDpMove | kDpCarryIn)) == 0u;
}

CERFENGINE_FORCEINLINE bool IsSlotAlu(const DecodedInsn& d) {
    const DpInfo& info = kDpInfo[d.op1];
    return IsFusableDp(info) && d.s == 0u && d.rn != ArmGpr::kR15 && d.rd != ArmGpr::kR15 &&
           (info.flags & kDpTest) == 0u;
}

CERFENGINE_FORCEINLINE bool IsFlagSlotAlu(const DecodedInsn& d) {
    const DpInfo& info = kDpInfo[d.op1];
    return IsFusableDp(info) && d.s != 0u && d.rn != ArmGpr::kR15 &&
           ((info.flags & kDpTest) != 0u || d.rd != ArmGpr::kR15);
}

CERFENGINE_FORCEINLINE uint32_t DpDest(const DecodedInsn& d) {
    return (kDpInfo[d.op1].flags & kDpTest) != 0u ? cerfir::kNoDest
                                                  : static_cast<uint32_t>(ArmGprDisp(d.rd));
}

CERFENGINE_FORCEINLINE bool IsShiftedSlotAlu(const DecodedInsn& d) {
    const DpInfo& info = kDpInfo[d.op1];
    const bool    test = (info.flags & kDpTest) != 0u;
    const bool    s    = d.s != 0u;
    return IsFusableDp(info) && d.rs != 0u && d.rs < 32u && d.n != kSrRrx &&
           d.rm != ArmGpr::kR15 && d.rn != ArmGpr::kR15 && (test ? s : d.rd != ArmGpr::kR15) &&
           (!s || (info.flags & kDpArith) != 0u);
}

}

void ArmIrDataProcessing(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    CheckDpOpcode(d);
    if (IsPlainMove(d)) {
        ArmIrWriteGprConst(c, d.rd, d.immediate);
        return;
    }
    if (IsSlotAlu(d)) {
        c.b->SlotImmInto(kDpInfo[d.op1].op, static_cast<uint32_t>(ArmGprDisp(d.rn)), d.immediate,
                         static_cast<uint32_t>(ArmGprDisp(d.rd)), false);
        return;
    }
    if (IsFlagSlotAlu(d)) {
        const DpInfo& info = kDpInfo[d.op1];
        c.b->SlotImmFlags(info.op, static_cast<uint32_t>(ArmGprDisp(d.rn)), d.immediate,
                          DpStoreMask(info, true), DpDest(d));
        if (!IsArith(d.op1) && d.rs != 0u) {
            ArmIrWriteFlag(c, ArmIrFlag::kC, c.b->Const1((d.immediate & 0x80000000u) != 0u));
        }
        return;
    }
    GuardExceptionReturn(c);
    DpOperand2         op2{c.b->Const32(d.immediate), kNoRef};
    if (d.s != 0u && !IsArith(d.op1) && d.rs != 0u) {
        op2.carry = c.b->Const1((d.immediate & 0x80000000u) != 0u);
    }
    ApplyDp(c, op2);
}

void ArmIrDataProcessingReg(ArmIrContext& c) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    CheckDpOpcode(d);
    if (IsPlainMove(d) && d.rs == 0u) {
        if (d.rm == ArmGpr::kR15) {
            b.SetSlot(static_cast<uint32_t>(ArmGprDisp(d.rd)), ArmIrReadGpr(c, d.rm));
        } else {
            b.SlotCopy32(static_cast<uint32_t>(ArmGprDisp(d.rd)), static_cast<uint32_t>(ArmGprDisp(d.rm)),
                         false);
        }
        return;
    }
    if (IsPlainMove(d) && d.rs != 0u && d.rs < 32u && d.n != kSrRrx && d.rm != ArmGpr::kR15) {
        b.SlotImmInto(ShiftOp(d.n), static_cast<uint32_t>(ArmGprDisp(d.rm)), d.rs,
                      static_cast<uint32_t>(ArmGprDisp(d.rd)), false);
        return;
    }
    if (IsSlotAlu(d) && d.rs == 0u && d.rm != ArmGpr::kR15) {
        b.SlotSlotInto(kDpInfo[d.op1].op, static_cast<uint32_t>(ArmGprDisp(d.rn)),
                       static_cast<uint32_t>(ArmGprDisp(d.rm)),
                       static_cast<uint32_t>(ArmGprDisp(d.rd)), false);
        return;
    }
    if (IsFlagSlotAlu(d) && d.rs == 0u && d.rm != ArmGpr::kR15) {
        const DpInfo& info = kDpInfo[d.op1];
        b.SlotSlotFlags(info.op, static_cast<uint32_t>(ArmGprDisp(d.rn)),
                        static_cast<uint32_t>(ArmGprDisp(d.rm)), DpStoreMask(info, true), DpDest(d));
        return;
    }
    if (IsShiftedSlotAlu(d)) {
        const DpInfo& info = kDpInfo[d.op1];
        const bool    test = (info.flags & kDpTest) != 0u;
        b.ShiftedSlotInto(info.op, static_cast<uint32_t>(ArmGprDisp(d.rn)),
                          static_cast<uint32_t>(ArmGprDisp(d.rm)), ShiftOp(d.n), d.rs,
                          DpStoreMask(info, d.s != 0u),
                          test ? cerfir::kNoDest : static_cast<uint32_t>(ArmGprDisp(d.rd)));
        return;
    }
    GuardExceptionReturn(c);
    const Ref          rm      = ArmIrReadGpr(c, d.rm);
    const uint8_t      want_c  = ShifterCarryWant(d);
    DpOperand2         op2{rm, kNoRef};
    if (d.n == kSrRrx) {
        FlagSet f;
        op2.value = b.Compute(Op::kRrx, rm, ArmIrReadFlag(c, ArmIrFlag::kC), kNoRef, want_c, &f);
        op2.carry = f.c;
    } else if (d.rs != 0u && want_c == 0u && d.rs < 32u) {
        op2.value = b.ComputeInto(ShiftOp(d.n), rm, b.Const32(d.rs), kNoRef, 0u, cerfir::kNoDest);
    } else if (d.rs != 0u) {
        FlagSet f;
        op2.value = b.Compute(ShiftCarryOp(d.n), rm, b.Const32(d.rs),
                              ArmIrReadFlag(c, ArmIrFlag::kC), want_c, &f);
        op2.carry = f.c;
    }
    ApplyDp(c, op2);
}

void ArmIrDataProcessingShiftedReg(ArmIrContext& c) {
    const DecodedInsn& d      = *c.d;
    cerfir::Builder&   b      = *c.b;
    CheckDpOpcode(d);
    GuardExceptionReturn(c);
    const Ref          rm     = ArmIrReadGpr(c, d.rm);
    const Ref          amount = b.Op2(Op::kAnd, ArmIrReadGpr(c, d.rs), b.Const32(0xFFu));
    FlagSet            f;
    DpOperand2         op2{kNoRef, kNoRef};
    op2.value = b.Compute(ShiftCarryOp(d.n), rm, amount, ArmIrReadFlag(c, ArmIrFlag::kC),
                          ShifterCarryWant(d), &f);
    op2.carry = f.c;
    ApplyDp(c, op2);
}

}
