#include <array>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using Operand     = X86RegAlloc::Operand;
using OperandKind = X86RegAlloc::OperandKind;

[[noreturn]] void SextFlagFault() {
    throw EmitFault("CerfIR x86 lowering: a sign-extended destination with a flag store");
}

CERFENGINE_FORCEINLINE void RequireRegisterWidth(const X86Lowering& l, Ref ref) {
    const Type t = l.GetBlock().At(ref).type;
    if (t != Type::kI32 && t != Type::kI1) {
        throw EmitFault("CerfIR x86 lowering: ALU type wider than a register");
    }
    if (t == Type::kI1 && l.WantedFlags(ref) != 0u) {
        throw EmitFault("CerfIR x86 lowering: flags of an i1 operation");
    }
}

CERFENGINE_FORCEINLINE void EmitAluOperand(X86RegAlloc& ra, uint8_t*& c, Op op, uint8_t dst,
                                           Ref src_ref) {
    Operand o = ra.Rm(src_ref);
    const bool has_mem_form = op == Op::kAdd || op == Op::kSub || op == Op::kOr ||
                              op == Op::kAnd || op == Op::kXor || op == Op::kAdc ||
                              op == Op::kSbc;
    if (o.kind == OperandKind::kSlot && !has_mem_form) {
        o = Operand{OperandKind::kReg, ra.MaterializeWide(src_ref, kX86AllocRegs), 0, 0u};
    }
    X86EmitAluOperand(c, op, dst, o);
    ra.Consume(src_ref);
}

CERFENGINE_FORCEINLINE void RecordArith(X86Lowering& l, Ref ref, uint8_t carry_cond) {
    X86FlagSource& f = l.FlagSource();
    f.producer       = ref;
    f.kind[0] = f.kind[1] = f.kind[2] = f.kind[3] = X86FlagKind::kEflags;
    f.where[0]       = kX86CondS;
    f.where[1]       = kX86CondZ;
    f.where[2]       = carry_cond;
    f.where[3]       = kX86CondO;
}

}

bool X86PreferSwap(X86RegAlloc& ra, Ref a, Ref b) {
    if (IsConstRef(a)) return !IsConstRef(b);
    if (IsConstRef(b)) return false;
    const bool a_takeable = IsValueRef(a) && ra.At(a).kind == X86RegAlloc::Kind::kReg &&
                            ra.Remaining(a) == 1u;
    const bool b_takeable = IsValueRef(b) && ra.At(b).kind == X86RegAlloc::Kind::kReg &&
                            ra.Remaining(b) == 1u;
    return b_takeable && !a_takeable;
}

void X86SetCarryFromValue(X86Lowering& l, Ref carry, bool want_cf_equal) {
    const Block& b  = l.GetBlock();
    X86RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (!IsConstRef(carry) && ra.At(carry).kind == X86RegAlloc::Kind::kFlags) {
        const uint8_t cond = ra.At(carry).cond;
        if (cond == kX86CondC || cond == kX86CondNc) {
            if ((cond == kX86CondC) != want_cf_equal) x86::EmitCmc(c);
            ra.Consume(carry);
            return;
        }
    }
    if (IsConstRef(carry)) {
        const uint8_t t = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegImm32(c, t, static_cast<uint32_t>(b.ConstValue(carry) & 1u));
        x86::EmitCmpRegImm8(c, t, 1u);
        ra.ReleaseTemp(t);
    } else if (ra.At(carry).kind == X86RegAlloc::Kind::kSlot) {
        x86::EmitCmpByteBaseDisp32Imm8(c, x86::kStateReg, ra.At(carry).disp, 1u);
    } else {
        const uint8_t reg = ra.Materialize(carry, kX86AllocRegs);
        if (ra.At(carry).wide) {
            x86::EmitCmpRegImm8(c, reg, 1u);
        } else {
            x86::EmitCmpReg8Imm8(c, reg, 1u);
        }
    }
    if (want_cf_equal) x86::EmitCmc(c);
    ra.Consume(carry);
}

template <Op kOp>
void X86LowerBinaryOp(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    if (inst.type == Type::kI64) {
        X86LowerWideAdd(l, ref);
        return;
    }
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobber();

    Ref a  = inst.args[0];
    Ref bb = inst.args[1];
    if constexpr (kOp != Op::kSub) {
        if (X86PreferSwap(ra, a, bb)) {
            const Ref t = a;
            a           = bb;
            bb          = t;
        }
    }
    const uint8_t dst = ra.TakeOrCopy(a, ref, kX86AllocRegs);
    EmitAluOperand(ra, l.Cursor(), kOp, dst, bb);
    ra.DefineInReg(ref, dst, true);
    if constexpr (kOp == Op::kAdd)      RecordArith(l, ref, kX86CondC);
    else if constexpr (kOp == Op::kSub) RecordArith(l, ref, kX86CondNc);
    else                                X86RecordNz(l, ref);
}

namespace {

template <Op kOp>
void EmitFusedAluOp(X86Lowering& l, Ref a, Ref b, uint32_t mask, int32_t dest, bool sext) {
    X86RegAlloc&  ra = l.Regs();
    ra.BeforeFlagsClobber();
    const uint8_t dst = ra.TakeOrCopy(a, kNoRef, kX86AllocRegs);
    EmitAluOperand(ra, l.Cursor(), kOp, dst, b);
    if (dest != static_cast<int32_t>(kNoDest)) {
        ra.BeforeSlotWrite(dest, sext ? 8u : 4u);
        x86::EmitMovBaseDisp32Reg(l.Cursor(), x86::kStateReg, dest, dst);
        if (sext) {
            if (mask != 0u) SextFlagFault();
            X86StoreSignWord(l, dst, dest + 4, true);
        }
    }
    ra.ReleaseTemp(dst);
    ra.EndOp();
    l.ClearFlagSource();
    if (mask == 0u) return;
    static constexpr uint8_t kWhere[4] = {kX86CondS, kX86CondZ, X86FusedCarryCond(kOp), kX86CondO};
    X86CheckFusedFlags(kOp, mask);
    X86StoreEflagsFlags(l, mask, kWhere);
}

}

void X86FusedFlagFault() {
    throw EmitFault("CerfIR x86 lowering: a fused logic op stores C or V");
}

void X86EmitFusedAlu(X86Lowering& l, Op op, Ref a, Ref b, uint32_t mask, int32_t dest,
                     bool sext) {
    switch (op) {
    case Op::kAdd: EmitFusedAluOp<Op::kAdd>(l, a, b, mask, dest, sext); return;
    case Op::kSub: EmitFusedAluOp<Op::kSub>(l, a, b, mask, dest, sext); return;
    case Op::kAnd: EmitFusedAluOp<Op::kAnd>(l, a, b, mask, dest, sext); return;
    case Op::kOr:  EmitFusedAluOp<Op::kOr>(l, a, b, mask, dest, sext); return;
    case Op::kXor: EmitFusedAluOp<Op::kXor>(l, a, b, mask, dest, sext); return;
    default:
        throw EmitFault("CerfIR x86 lowering: a fused ALU op is not Add, Sub, And, Or or Xor");
    }
}

template void X86LowerBinaryOp<Op::kAdd>(X86Lowering& l, Ref ref);
template void X86LowerBinaryOp<Op::kSub>(X86Lowering& l, Ref ref);
template void X86LowerBinaryOp<Op::kAnd>(X86Lowering& l, Ref ref);
template void X86LowerBinaryOp<Op::kOr>(X86Lowering& l, Ref ref);
template void X86LowerBinaryOp<Op::kXor>(X86Lowering& l, Ref ref);

void X86LowerCarryArith(X86Lowering& l, Ref ref) {
    const Block& b     = l.GetBlock();
    const Inst&  inst  = b.At(ref);
    X86RegAlloc& ra    = l.Regs();
    const Ref    carry = inst.args[2];
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobberExcept(carry);

    const uint8_t dst = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    const bool    adc = inst.op == Op::kAdc;
    const Operand o   = ra.Rm(inst.args[1]);
    if (o.kind == OperandKind::kReg) ra.Lock(o.reg);
    X86SetCarryFromValue(l, carry, adc);
    EmitAluOperand(ra, l.Cursor(), inst.op, dst, inst.args[1]);
    ra.DefineInReg(ref, dst, true);
    RecordArith(l, ref, adc ? kX86CondC : kX86CondNc);
}

void X86LowerInvertedLogic(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobber();

    const bool    and_not = inst.op == Op::kAndNot;
    const uint8_t dst     = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    const Ref     src     = inst.args[1];
    if (IsConstRef(src)) {
        const uint32_t inverted = ~static_cast<uint32_t>(b.ConstValue(src)) &
                                  static_cast<uint32_t>(TypeMask(inst.type));
        if (and_not) x86::EmitAndRegImm32(c, dst, inverted);
        else         x86::EmitOrRegImm32(c, dst, inverted);
    } else {
        const uint8_t t = ra.TakeOrCopy(src, ref, kX86AllocRegs);
        x86::EmitNotReg32(c, t);
        if (inst.type == Type::kI1) x86::EmitAndRegImm32(c, t, 1u);
        if (and_not) x86::EmitAndReg32Reg32(c, dst, t);
        else         x86::EmitOrReg32Reg32(c, dst, t);
        ra.ReleaseTemp(t);
    }
    ra.DefineInReg(ref, dst, true);
    X86RecordNz(l, ref);
}

void X86LowerUnary(X86Lowering& l, Ref ref) {
    const Block&  b    = l.GetBlock();
    const Inst&   inst = b.At(ref);
    X86RegAlloc&  ra   = l.Regs();
    uint8_t*&     c    = l.Cursor();
    const uint8_t want = l.WantedFlags(ref);
    if (inst.type == Type::kI64 && inst.op == Op::kNeg) {
        X86LowerWideNeg(l, ref);
        return;
    }
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobber();

    const uint8_t dst = ra.TakeOrCopy(inst.args[0], ref, kX86AllocRegs);
    switch (inst.op) {
    case Op::kNot:
        x86::EmitNotReg32(c, dst);
        if (inst.type == Type::kI1) x86::EmitAndRegImm32(c, dst, 1u);
        break;
    case Op::kNeg:
        x86::EmitNegReg32(c, dst);
        break;
    case Op::kCopy:
        break;
    case Op::kByteSwap:
        x86::EmitBswapReg32(c, dst);
        break;
    default:
        throw EmitFault("CerfIR x86 lowering: not a unary op");
    }
    if ((want & 3u) != 0u) {
        x86::EmitTestRegReg(c, dst, dst);
        X86RecordNz(l, ref);
    }
    ra.DefineInReg(ref, dst, true);
}

void X86LowerExtend(X86Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    arg  = inst.args[0];
    if (inst.op == Op::kZeroExtend && inst.type == Type::kI32 && IsValueRef(arg) &&
        b.TypeOf(arg) == Type::kI1) {
        ra.DefineInReg(ref, ra.TakeOrCopy(arg, ref, kX86AllocRegs), true);
        return;
    }
    if (inst.type != Type::kI32 || IsConstRef(arg) || b.At(arg).op != Op::kTruncate) {
        throw EmitFault("CerfIR x86 lowering: extension of a value other than a truncated register");
    }
    const Type from = b.At(arg).type;
    const Ref  src  = b.At(arg).args[0];
    if ((from != Type::kI8 && from != Type::kI16) || b.TypeOf(src) != Type::kI32) {
        throw EmitFault("CerfIR x86 lowering: extension width has no x86-32 lowering");
    }
    const bool    byte = from == Type::kI8;
    const bool    sign = inst.op == Op::kSignExtend;
    const uint8_t s    = ra.MaterializeWide(src, byte ? kX86ByteRegs : kX86AllocRegs);
    const uint8_t d    = ra.Remaining(src) == 1u ? s : ra.AllocTemp(kX86AllocRegs);
    if (byte) {
        if (sign) x86::EmitMovsxReg32Reg8(c, d, s);
        else      x86::EmitMovzxReg32Reg8(c, d, s);
    } else {
        if (sign) x86::EmitMovsxReg32Reg16(c, d, s);
        else      x86::EmitMovzxReg32Reg16(c, d, s);
    }
    ra.Consume(src);
    ra.DefineInReg(ref, d, true);
}

/* ARM DDI 0406C.c D16.5 (p. D16-2655): CountLeadingZeroBits(x) = N - 1 -
   HighestSetBit(x), and HighestSetBit(x) = -1 when x is zero. */
void X86LowerClz(X86Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobber();

    const uint8_t src = ra.MaterializeWide(inst.args[0], kX86AllocRegs);
    const uint8_t dst = ra.AllocTemp(kX86AllocRegs);
    const uint8_t idx = ra.AllocTemp(kX86AllocRegs);
    x86::EmitMovRegImm32(c, dst, 32u);
    x86::EmitTestRegReg(c, src, src);
    uint8_t* zero = x86::EmitJzLabel(c);
    x86::EmitBsrReg32Reg32(c, idx, src);
    x86::EmitMovRegImm32(c, dst, 31u);
    x86::EmitSubReg32Reg32(c, dst, idx);
    x86::FixupLabel(zero, c);
    ra.ReleaseTemp(idx);
    ra.Consume(inst.args[0]);
    ra.DefineInReg(ref, dst, true);
}

}
