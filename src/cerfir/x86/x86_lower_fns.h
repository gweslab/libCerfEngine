#pragma once

#include <array>
#include <cstddef>

#include "../../emit_fault.h"
#include <cerfengine/force_inline.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_pointer.h"
#include "../../x86_emit_shift.h"
#include "../cerfir_op.h"
#include "x86_host_abi.h"
#include "x86_lowering.h"

namespace cerfengine::cerfir {

using X86LowerFn = void (*)(X86Lowering& lowering, Ref ref);

extern const std::array<X86LowerFn, static_cast<size_t>(Op::kCount)> kX86LowerTable;

inline X86LowerFn X86LowerFnFor(Op op) { return kX86LowerTable[static_cast<size_t>(op)]; }

void X86LowerGetSlot(X86Lowering& lowering, Ref ref);

CERFENGINE_FORCEINLINE void X86StoreSlot(X86Lowering& l, Ref value, int32_t disp) {
    const Block&   b     = l.GetBlock();
    const uint32_t bytes = X86SlotBytes(b.TypeOf(value));
    X86RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    if (bytes != 1u && bytes != 4u) throw EmitFault("CerfIR x86 lowering: slot width has no store");

    ra.BeforeSlotWrite(disp, bytes);
    if (IsConstRef(value)) {
        const uint64_t v = b.ConstValue(value);
        if (bytes == 4u) {
            x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, disp, static_cast<uint32_t>(v));
        } else {
            x86::EmitMovByteBaseDisp32Imm8(c, x86::kStateReg, disp, static_cast<uint8_t>(v));
        }
        return;
    }
    if (bytes == 1u && IsValueRef(value) && ra.At(value).kind == X86RegAlloc::Kind::kFlags) {
        x86::EmitSetccBaseDisp32(c, static_cast<uint8_t>(x86::kSetO + ra.At(value).cond),
                                 x86::kStateReg, disp);
        ra.Consume(value);
        return;
    }
    if (bytes == 1u) {
        const uint8_t reg = ra.Materialize(value, kX86ByteRegs);
        x86::EmitMovBaseDisp32Byte(c, x86::kStateReg, disp, reg);
    } else {
        const uint8_t reg = ra.MaterializeWide(value, kX86AllocRegs);
        x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, disp, reg);
    }
    ra.NoteStored(value, disp);
    ra.Consume(value);
}

CERFENGINE_FORCEINLINE void X86StoreSignWord(X86Lowering& l, uint8_t reg, int32_t disp,
                                             bool reg_is_scratch) {
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    uint8_t      sign = reg;
    ra.BeforeFlagsClobber();
    l.ClearFlagSource();
    if (!reg_is_scratch) {
        sign = ra.FreeScratch();
        if (sign == X86RegAlloc::kNoReg) sign = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegReg(c, sign, reg);
    }
    x86::EmitSarReg32Imm(c, sign, 31u);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, disp, sign);
    if (!reg_is_scratch) ra.ReleaseTemp(sign);
}

CERFENGINE_FORCEINLINE void X86StoreSlotSext(X86Lowering& l, Ref value, int32_t disp) {
    const Block& b  = l.GetBlock();
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (b.TypeOf(value) != Type::kI32) {
        throw EmitFault("CerfIR x86 lowering: sign-extended slot store of a value other than i32");
    }
    ra.BeforeSlotWrite(disp, 8u);
    if (IsConstRef(value)) {
        const uint32_t v = static_cast<uint32_t>(b.ConstValue(value));
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, disp, v);
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, disp + 4,
                                    (v & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u);
        return;
    }
    if (IsSlotRef(value)) {
        ra.BeforeFlagsClobber();
        uint8_t reg = ra.FreeScratch();
        if (reg == X86RegAlloc::kNoReg) {
            reg = ra.MaterializeWide(value, kX86AllocRegs);
        } else {
            x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, SlotRefDisp(value));
        }
        x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, disp, reg);
        X86StoreSignWord(l, reg, disp + 4, true);
        return;
    }
    const uint8_t reg = ra.MaterializeWide(value, kX86AllocRegs);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, disp, reg);
    X86StoreSignWord(l, reg, disp + 4, false);
    ra.NoteStored(value, disp);
    ra.Consume(value);
}

struct X86CallSite {
    const void*    fn;
    CallConv       conv;
    uint32_t       num_args;
    Ref            args[3];
    const CallArg* extra;
    uint32_t       num_extra;
    bool           store_pc;
    uint32_t       pc;
    Ref            result;
    Type           result_type;
};

CERFENGINE_FORCEINLINE uint32_t X86CallArgImm(const CallArg& a) {
    return static_cast<uint32_t>(a.value);
}

CERFENGINE_FORCEINLINE void X86EmitPushCallArg(uint8_t*& c, const CallArg& a) {
    if (a.kind == CallArgKind::kHostWord) {
        x86::EmitPushDwordPtr(c, a.Host());
    } else {
        x86::EmitPush32(c, X86CallArgImm(a));
    }
}

CERFENGINE_FORCEINLINE void X86LoadCallArg(uint8_t*& c, const CallArg& a, uint8_t reg) {
    switch (a.kind) {
    case CallArgKind::kImm:         x86::EmitMovRegImm32(c, reg, X86CallArgImm(a)); return;
    case CallArgKind::kHostAddress: x86::EmitMovPtrRegImm(c, reg, a.Host()); return;
    case CallArgKind::kHostWord:    x86::EmitMovRegDwordPtr(c, reg, a.Host()); return;
    }
    throw EmitFault("CerfIR x86 lowering: a call argument has no load");
}

bool X86EmitLeanCall(X86Lowering& lowering, const X86CallSite& site);
void X86EmitCall(X86Lowering& lowering, const X86CallSite& site);
bool X86EmitLeanCall32(X86Lowering& lowering, const X86CallSite& site);
void X86EmitCall32(X86Lowering& lowering, const X86CallSite& site);
void X86EmitCallX64(X86Lowering& lowering, const X86CallSite& site);
void X86LowerCall(X86Lowering& lowering, Ref ref);
void X86LowerLoad(X86Lowering& lowering, Ref ref);
void X86LowerSwap(X86Lowering& lowering, Ref ref);
void X86CallLoad(X86Lowering& lowering, Ref ref, Ref address, const MemAttr& attr);
void X86CallStore(X86Lowering& lowering, Ref address, Ref value, const MemAttr& attr);

template <Op kOp>
void X86LowerBinaryOp(X86Lowering& lowering, Ref ref);
void X86LowerCarryArith(X86Lowering& lowering, Ref ref);
void X86LowerInvertedLogic(X86Lowering& lowering, Ref ref);
void X86LowerUnary(X86Lowering& lowering, Ref ref);
void X86LowerClz(X86Lowering& lowering, Ref ref);
void X86LowerExtend(X86Lowering& lowering, Ref ref);
void X86LowerSaturate(X86Lowering& lowering, Ref ref);
void X86LowerOrSticky(X86Lowering& lowering, Ref ref);
void X86LowerSignedSat(X86Lowering& lowering, Ref ref);
void X86LowerHostLoad(X86Lowering& lowering, Ref ref);
void X86LowerHostStore(X86Lowering& lowering, Ref ref);
void X86LowerHostTest(X86Lowering& lowering, Ref ref);
void X86LowerHostReplace(X86Lowering& lowering, Ref ref);
void X86LowerSelect(X86Lowering& lowering, Ref ref);

void X86LowerShiftCarry(X86Lowering& lowering, Ref ref);
void X86LowerRrx(X86Lowering& lowering, Ref ref);
void X86LowerShiftModulo(X86Lowering& lowering, Ref ref);

void X86LowerMul(X86Lowering& lowering, Ref ref);
void X86LowerMulWide(X86Lowering& lowering, Ref ref);
void X86LowerDivRem(X86Lowering& lowering, Ref ref);
void X86LowerHalf(X86Lowering& lowering, Ref ref);
void X86LowerWideAdd(X86Lowering& lowering, Ref ref);
void X86LowerWideNeg(X86Lowering& lowering, Ref ref);
void X86LowerWideShift(X86Lowering& lowering, Ref ref);
uint8_t X86TakeHalf(X86Lowering& lowering, const X86WideHalf& half, Ref def);

CERFENGINE_FORCEINLINE X86RegAlloc::Operand X86HalfOperand(X86RegAlloc& ra, const X86WideHalf& h) {
    if (h.imm) return X86RegAlloc::Operand{X86RegAlloc::OperandKind::kImm, 0u, 0, h.value};
    return ra.Rm(h.ref);
}
void X86RecordWideNz(X86Lowering& lowering, Ref ref, uint8_t lo, uint8_t hi);

void X86LowerCompare(X86Lowering& lowering, Ref ref);
void X86LowerFlag(X86Lowering& lowering, Ref ref);
void X86StoreProducerFlags(X86Lowering& lowering, Ref producer);
void X86StoreFlagsFrom(X86Lowering& lowering, uint32_t mask, const X86FlagSource& source);
void X86StoreEflagsFlags(X86Lowering& lowering, uint32_t mask, const uint8_t (&where)[4]);
void X86LowerUnsupported(X86Lowering& lowering, Ref ref);

void X86SetCarryFromValue(X86Lowering& lowering, Ref carry, bool want_cf_equal);
bool X86PreferSwap(X86RegAlloc& ra, Ref a, Ref b);

CERFENGINE_FORCEINLINE void X86RecordNz(X86Lowering& l, Ref ref) {
    X86FlagSource& f = l.FlagSource();
    if (f.producer != ref) {
        f.producer = ref;
        f.kind[2]  = X86FlagKind::kNone;
        f.kind[3]  = X86FlagKind::kNone;
    }
    f.kind[0]  = X86FlagKind::kEflags;
    f.kind[1]  = X86FlagKind::kEflags;
    f.where[0] = kX86CondS;
    f.where[1] = kX86CondZ;
}

void X86EmitSetSlot(X86Lowering& lowering, Ref value, int32_t disp, bool sext);
void X86EmitLabel(X86Lowering& lowering, uint32_t label);
void X86EmitBranch(X86Lowering& lowering, Ref cond, uint32_t label, bool if_true);
void X86EmitBranchSlots(X86Lowering& lowering, const SlotTest& test, uint32_t label);
uint8_t* X86EmitGuardBranch(X86Lowering& lowering, const SlotTest& test);
void     X86EmitSetSlotTest(X86Lowering& lowering, const SlotTest& test, int32_t dest);
void X86EmitExitDirect(X86Lowering& lowering, uint32_t target, uint16_t flags);
void X86EmitExitIndirect(X86Lowering& lowering);
void X86EmitExitDispatcher(X86Lowering& lowering);
void X86EmitFusedAlu(X86Lowering& lowering, Op op, Ref a, Ref b, uint32_t mask, int32_t dest,
                     bool sext);
void X86EmitLoad(X86Lowering& lowering, Ref ref, Ref address, Ref writeback, uint32_t attr);
void X86EmitStore(X86Lowering& lowering, Ref address, Ref value, Ref writeback, uint32_t attr);
void X86EmitTransferWords(X86Lowering& lowering, uint32_t attr, uint32_t list);

CERFENGINE_FORCEINLINE void X86EmitAluOperand(uint8_t*& c, Op op, uint8_t dst,
                                              const X86RegAlloc::Operand& o) {
    using OperandKind = X86RegAlloc::OperandKind;
    switch (op) {
    case Op::kAdd:
        if (o.kind == OperandKind::kReg)       x86::EmitAddReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitAddRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitAddRegImm32(c, dst, o.imm);
        break;
    case Op::kSub:
        if (o.kind == OperandKind::kReg)       x86::EmitSubReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitSubRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitSubRegImm32(c, dst, o.imm);
        break;
    case Op::kAdc:
        if (o.kind == OperandKind::kReg)       x86::EmitAdcReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitAdcRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitAdcRegImm32(c, dst, o.imm);
        break;
    case Op::kSbc:
        if (o.kind == OperandKind::kReg)       x86::EmitSbbReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitSbbRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitSbbRegImm32(c, dst, o.imm);
        break;
    case Op::kAnd:
        if (o.kind == OperandKind::kReg)       x86::EmitAndReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitAndRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitAndRegImm32(c, dst, o.imm);
        break;
    case Op::kOr:
        if (o.kind == OperandKind::kReg)       x86::EmitOrReg32Reg32(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitOrRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitOrRegImm32(c, dst, o.imm);
        break;
    case Op::kXor:
        if (o.kind == OperandKind::kReg)       x86::EmitXorRegReg(c, dst, o.reg);
        else if (o.kind == OperandKind::kSlot) x86::EmitXorRegBaseDisp32(c, dst, x86::kStateReg, o.disp);
        else                                   x86::EmitXorRegImm32(c, dst, o.imm);
        break;
    case Op::kShl:
    case Op::kLsr:
    case Op::kAsr:
    case Op::kRor:
        if (o.kind != OperandKind::kImm || o.imm == 0u || o.imm >= 32u) {
            throw EmitFault("CerfIR x86 lowering: a two-operand shift takes a constant amount of 1 to 31");
        }
        if (op == Op::kShl)      x86::EmitShlReg32Imm(c, dst, static_cast<uint8_t>(o.imm));
        else if (op == Op::kLsr) x86::EmitShrReg32Imm(c, dst, static_cast<uint8_t>(o.imm));
        else if (op == Op::kAsr) x86::EmitSarReg32Imm(c, dst, static_cast<uint8_t>(o.imm));
        else                     x86::EmitRorReg32Imm(c, dst, static_cast<uint8_t>(o.imm));
        break;
    default:
        throw EmitFault("CerfIR x86 lowering: not a two-operand ALU op");
    }
}

[[noreturn]] void X86FusedFlagFault();

CERFENGINE_FORCEINLINE constexpr uint8_t X86FusedCarryCond(Op op) {
    return op == Op::kAdd ? kX86CondC : kX86CondNc;
}

CERFENGINE_FORCEINLINE void X86CheckFusedFlags(Op op, uint32_t mask) {
    if (op != Op::kAdd && op != Op::kSub && (mask & 0x0Cu) != 0u) X86FusedFlagFault();
}

}
