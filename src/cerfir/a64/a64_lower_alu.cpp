#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using Operand = A64RegAlloc::Operand;

[[noreturn]] void SextFlagFault() {
    throw EmitFault("CerfIR A64 lowering: a sign-extended destination with a flag store");
}

CERFENGINE_FORCEINLINE void RequireRegisterWidth(const A64Lowering& l, Ref ref) {
    const Type t = l.GetBlock().At(ref).type;
    if (t != Type::kI32 && t != Type::kI1) {
        throw EmitFault("CerfIR A64 lowering: ALU type wider than a register");
    }
    if (t == Type::kI1 && l.WantedFlags(ref) != 0u) {
        throw EmitFault("CerfIR A64 lowering: flags of an i1 operation");
    }
}

CERFENGINE_FORCEINLINE bool IsCommutative(Op op) {
    return op == Op::kAdd || op == Op::kAnd || op == Op::kOr || op == Op::kXor;
}

CERFENGINE_FORCEINLINE void EmitTstSelf(uint8_t*& c, uint8_t reg) {
    a64::EmitTstReg(c, reg, reg);
}

CERFENGINE_FORCEINLINE void EmitLogicOperand(uint8_t*& c, a64::LogicOp op, uint8_t dst, uint8_t lhs,
                                             const Operand& rhs) {
    if (rhs.imm) {
        a64::EmitLogicImm(c, op, dst, lhs, rhs.value);
    } else {
        a64::EmitLogicReg(c, op, dst, lhs, rhs.reg);
    }
}

}

bool A64AluImmFits(Op op, uint32_t imm) {
    switch (op) {
    case Op::kAdd:
    case Op::kSub:
        return a64::AddSubImmFits(imm) || a64::AddSubImmFits(0u - imm);
    case Op::kAnd:
    case Op::kOr:
    case Op::kXor:
        return a64::LogicImmFits(imm, false);
    case Op::kAndNot:
    case Op::kOrNot:
        return a64::LogicImmFits(static_cast<uint32_t>(~imm), false);
    default:
        return false;
    }
}

A64RegAlloc::Operand A64AluOperand(A64RegAlloc& ra, Op op, Ref r) {
    if (IsConstRef(r)) {
        const uint32_t v = ra.ConstOf(r);
        if (v == 0u && op != Op::kAndNot && op != Op::kOrNot) return ra.Reg(a64::kZr);
        if (A64AluImmFits(op, v)) return ra.Imm(v);
    }
    return ra.Reg(ra.Materialize(r));
}

void A64EmitAlu(uint8_t*& c, Op op, uint8_t dst, uint8_t lhs, const Operand& rhs, bool set_flags) {
    switch (op) {
    case Op::kAdd:
    case Op::kSub: {
        const bool add = op == Op::kAdd;
        if (!rhs.imm) {
            if (add) {
                set_flags ? a64::EmitAddsReg(c, dst, lhs, rhs.reg) : a64::EmitAddReg(c, dst, lhs, rhs.reg);
            } else {
                set_flags ? a64::EmitSubsReg(c, dst, lhs, rhs.reg) : a64::EmitSubReg(c, dst, lhs, rhs.reg);
            }
            return;
        }
        uint32_t imm  = rhs.value;
        bool     as_add = add;
        if (!a64::AddSubImmFits(imm)) {
            imm    = 0u - imm;
            as_add = !add;
        }
        if (as_add) {
            set_flags ? a64::EmitAddsImm(c, dst, lhs, imm) : a64::EmitAddImm(c, dst, lhs, imm);
        } else {
            set_flags ? a64::EmitSubsImm(c, dst, lhs, imm) : a64::EmitSubImm(c, dst, lhs, imm);
        }
        return;
    }
    case Op::kAdc:
    case Op::kSbc:
        if (rhs.imm) throw EmitFault("CerfIR A64 lowering: a carry op takes a register operand");
        if (op == Op::kAdc) {
            set_flags ? a64::EmitAdcs(c, dst, lhs, rhs.reg) : a64::EmitAdc(c, dst, lhs, rhs.reg);
        } else {
            set_flags ? a64::EmitSbcs(c, dst, lhs, rhs.reg) : a64::EmitSbc(c, dst, lhs, rhs.reg);
        }
        return;
    case Op::kAnd:
        EmitLogicOperand(c, set_flags ? a64::kAnds : a64::kAnd, dst, lhs, rhs);
        return;
    case Op::kOr:
    case Op::kXor:
        EmitLogicOperand(c, op == Op::kOr ? a64::kOrr : a64::kEor, dst, lhs, rhs);
        if (set_flags) EmitTstSelf(c, dst);
        return;
    case Op::kAndNot:
        if (rhs.imm) {
            a64::EmitLogicImm(c, set_flags ? a64::kAnds : a64::kAnd, dst, lhs,
                              static_cast<uint32_t>(~rhs.value));
        } else {
            a64::EmitLogicReg(c, set_flags ? a64::kBics : a64::kBic, dst, lhs, rhs.reg);
        }
        return;
    case Op::kOrNot:
        if (rhs.imm) {
            a64::EmitLogicImm(c, a64::kOrr, dst, lhs, static_cast<uint32_t>(~rhs.value));
        } else {
            a64::EmitLogicReg(c, a64::kOrn, dst, lhs, rhs.reg);
        }
        if (set_flags) EmitTstSelf(c, dst);
        return;
    case Op::kShl:
    case Op::kLsr:
    case Op::kAsr:
    case Op::kRor:
        if (!rhs.imm || rhs.value == 0u || rhs.value >= 32u) {
            throw EmitFault("CerfIR A64 lowering: a two-operand shift takes a constant amount of 1 to 31");
        }
        if (set_flags) throw EmitFault("CerfIR A64 lowering: flags of a modulo shift");
        if (op == Op::kShl)      a64::EmitLslImm(c, dst, lhs, rhs.value);
        else if (op == Op::kLsr) a64::EmitLsrImm(c, dst, lhs, rhs.value);
        else if (op == Op::kAsr) a64::EmitAsrImm(c, dst, lhs, rhs.value);
        else                     a64::EmitRorImm(c, dst, lhs, rhs.value);
        return;
    default:
        throw EmitFault("CerfIR A64 lowering: not a two-operand ALU op");
    }
}

void A64SetCarryFromValue(A64Lowering& l, Ref carry) {
    A64RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (IsConstRef(carry)) {
        if ((ra.ConstOf(carry) & 1u) != 0u) {
            a64::EmitCmpReg(c, a64::kZr, a64::kZr);
        } else {
            a64::EmitAddsReg(c, a64::kZr, a64::kZr, a64::kZr);
        }
        return;
    }
    const A64RegAlloc::Loc& loc = ra.At(carry);
    if (loc.kind == A64RegAlloc::Kind::kFlags && loc.cond == a64::kCs) {
        ra.Consume(carry);
        return;
    }
    if (loc.kind == A64RegAlloc::Kind::kSlot) {
        l.Pins().CheckUnpinned(loc.disp, 1u);
        a64::EmitLdrb(c, kA64Tmp0, a64::kStateReg, loc.disp);
        a64::EmitCmpImm(c, kA64Tmp0, 1u);
    } else {
        a64::EmitCmpImm(c, ra.Materialize(carry), 1u);
    }
    ra.Consume(carry);
}

template <Op kOp>
void A64LowerBinaryOp(A64Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    A64RegAlloc& ra   = l.Regs();
    if (inst.type == Type::kI64) {
        A64LowerWideAdd(l, ref);
        return;
    }
    RequireRegisterWidth(l, ref);
    const bool set = l.WantedFlags(ref) != 0u;
    if (set) ra.BeforeFlagsClobber();

    Ref a  = inst.args[0];
    Ref bb = inst.args[1];
    if (IsCommutative(kOp) && IsConstRef(a) && !IsConstRef(bb)) {
        const Ref t = a;
        a           = bb;
        bb          = t;
    }
    const uint8_t lhs = ra.Materialize(a);
    const Operand rhs = A64AluOperand(ra, kOp, bb);
    ra.Consume(a);
    ra.Consume(bb);
    const uint8_t dst = ra.DestReg(lhs, rhs.imm ? A64RegAlloc::kNoReg : rhs.reg);
    A64EmitAlu(l.Cursor(), kOp, dst, lhs, rhs, set);
    ra.DefineInReg(ref, dst);
    if (!set) return;
    if constexpr (kOp == Op::kAdd || kOp == Op::kSub) {
        A64RecordArith(l, ref);
    } else {
        A64RecordNz(l, ref);
    }
}

namespace {

template <Op kOp>
void EmitFusedAluOp(A64Lowering& l, Ref a, Ref b, uint32_t mask, int32_t dest, bool sext) {
    A64RegAlloc& ra  = l.Regs();
    uint8_t*&    c   = l.Cursor();
    const bool   set = mask != 0u;
    if (set) {
        A64CheckFusedFlags(kOp, mask);
        ra.BeforeFlagsClobber();
    }
    const uint8_t lhs = ra.Materialize(a);
    const Operand rhs = A64AluOperand(ra, kOp, b);
    ra.Consume(a);
    ra.Consume(b);
    const uint8_t dst = ra.DestReg(lhs, rhs.imm ? A64RegAlloc::kNoReg : rhs.reg);
    A64EmitAlu(c, kOp, dst, lhs, rhs, set);
    if (dest != static_cast<int32_t>(kNoDest)) {
        ra.BeforeSlotWrite(dest, sext ? 8u : 4u);
        l.Pins().Store(c, dst, dest);
        if (sext) {
            if (set) SextFlagFault();
            a64::EmitAsrImm(c, kA64Tmp0, dst, 31u);
            l.Pins().Store(c, kA64Tmp0, dest + 4);
        }
    }
    ra.ReleaseTemp(dst);
    ra.EndOp();
    l.ClearFlagSource();
    if (set) A64StoreNzcvFlags(l, mask, kA64ArithFlagConds);
}

}

void A64FusedFlagFault() {
    throw EmitFault("CerfIR A64 lowering: a fused logic op stores C or V");
}

void A64EmitFusedAlu(A64Lowering& l, Op op, Ref a, Ref b, uint32_t mask, int32_t dest, bool sext) {
    switch (op) {
    case Op::kAdd: EmitFusedAluOp<Op::kAdd>(l, a, b, mask, dest, sext); return;
    case Op::kSub: EmitFusedAluOp<Op::kSub>(l, a, b, mask, dest, sext); return;
    case Op::kAnd: EmitFusedAluOp<Op::kAnd>(l, a, b, mask, dest, sext); return;
    case Op::kOr:  EmitFusedAluOp<Op::kOr>(l, a, b, mask, dest, sext); return;
    case Op::kXor: EmitFusedAluOp<Op::kXor>(l, a, b, mask, dest, sext); return;
    default:
        throw EmitFault("CerfIR A64 lowering: a fused ALU op is not Add, Sub, And, Or or Xor");
    }
}

template void A64LowerBinaryOp<Op::kAdd>(A64Lowering& l, Ref ref);
template void A64LowerBinaryOp<Op::kSub>(A64Lowering& l, Ref ref);
template void A64LowerBinaryOp<Op::kAnd>(A64Lowering& l, Ref ref);
template void A64LowerBinaryOp<Op::kOr>(A64Lowering& l, Ref ref);
template void A64LowerBinaryOp<Op::kXor>(A64Lowering& l, Ref ref);

void A64LowerCarryArith(A64Lowering& l, Ref ref) {
    const Inst&  inst  = l.GetBlock().At(ref);
    A64RegAlloc& ra    = l.Regs();
    const Ref    carry = inst.args[2];
    RequireRegisterWidth(l, ref);
    ra.BeforeFlagsClobberExcept(carry);

    const uint8_t lhs = ra.Materialize(inst.args[0]);
    const uint8_t rhs = IsConstRef(inst.args[1]) && ra.ConstOf(inst.args[1]) == 0u
                            ? a64::kZr
                            : ra.Materialize(inst.args[1]);
    A64SetCarryFromValue(l, carry);
    ra.Consume(inst.args[0]);
    ra.Consume(inst.args[1]);
    const uint8_t dst = ra.DestReg(lhs, rhs);
    const bool    set = l.WantedFlags(ref) != 0u;
    A64EmitAlu(l.Cursor(), inst.op, dst, lhs, ra.Reg(rhs), set);
    ra.DefineInReg(ref, dst);
    if (set) {
        A64RecordArith(l, ref);
    } else {
        l.ClearFlagSource();
    }
}

void A64LowerInvertedLogic(A64Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    RequireRegisterWidth(l, ref);
    const bool set = l.WantedFlags(ref) != 0u;
    if (set) ra.BeforeFlagsClobber();

    const bool    and_not = inst.op == Op::kAndNot;
    const Ref     src     = inst.args[1];
    const uint8_t lhs     = ra.Materialize(inst.args[0]);
    if (IsConstRef(src)) {
        const uint32_t inverted =
            ~ra.ConstOf(src) & static_cast<uint32_t>(TypeMask(inst.type));
        uint8_t temp = A64RegAlloc::kNoReg;
        Operand rhs  = ra.Reg(a64::kZr);
        if (inverted != 0u && a64::LogicImmFits(inverted, false)) {
            rhs = ra.Imm(inverted);
        } else if (inverted != 0u) {
            temp = ra.AllocTemp();
            a64::EmitMovImm32(c, temp, inverted);
            rhs = ra.Reg(temp);
        }
        ra.Consume(inst.args[0]);
        const uint8_t dst = ra.DestReg(lhs);
        A64EmitAlu(c, and_not ? Op::kAnd : Op::kOr, dst, lhs, rhs, set);
        if (temp != A64RegAlloc::kNoReg) ra.ReleaseTemp(temp);
        ra.DefineInReg(ref, dst);
        if (set) A64RecordNz(l, ref);
        return;
    }
    const uint8_t rhs = ra.Materialize(src);
    ra.Consume(inst.args[0]);
    ra.Consume(src);
    const uint8_t dst = ra.DestReg(lhs, rhs);
    A64EmitAlu(c, inst.op, dst, lhs, ra.Reg(rhs), set);
    if (inst.type == Type::kI1 && !and_not) a64::EmitLogicImm(c, a64::kAnd, dst, dst, 1u);
    ra.DefineInReg(ref, dst);
    if (set) A64RecordNz(l, ref);
}

void A64LowerUnary(A64Lowering& l, Ref ref) {
    const Inst&   inst = l.GetBlock().At(ref);
    A64RegAlloc&  ra   = l.Regs();
    uint8_t*&     c    = l.Cursor();
    const uint8_t want = l.WantedFlags(ref);
    if (inst.type == Type::kI64 && inst.op == Op::kNeg) {
        A64LowerWideNeg(l, ref);
        return;
    }
    RequireRegisterWidth(l, ref);
    if ((want & 3u) != 0u) ra.BeforeFlagsClobber();

    const uint8_t src = ra.Materialize(inst.args[0]);
    ra.Consume(inst.args[0]);
    const uint8_t dst = ra.DestReg(src);
    switch (inst.op) {
    case Op::kNot:
        if (inst.type == Type::kI1) {
            a64::EmitLogicImm(c, a64::kEor, dst, src, 1u);
        } else {
            a64::EmitMvn(c, dst, src);
        }
        break;
    case Op::kNeg:
        a64::EmitNeg(c, dst, src);
        break;
    case Op::kCopy:
        if (dst != src) a64::EmitMovReg(c, dst, src);
        break;
    case Op::kByteSwap:
        a64::EmitRev(c, dst, src);
        break;
    default:
        throw EmitFault("CerfIR A64 lowering: not a unary op");
    }
    if ((want & 3u) != 0u) {
        a64::EmitTstReg(c, dst, dst);
        A64RecordNz(l, ref);
    }
    ra.DefineInReg(ref, dst);
}

void A64LowerExtend(A64Lowering& l, Ref ref) {
    const Block& b    = l.GetBlock();
    const Inst&  inst = b.At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Ref    arg  = inst.args[0];
    if (inst.op == Op::kZeroExtend && inst.type == Type::kI32 && IsValueRef(arg) &&
        b.TypeOf(arg) == Type::kI1) {
        const uint8_t src = ra.Materialize(arg);
        ra.Consume(arg);
        const uint8_t dst = ra.DestReg(src);
        if (dst != src) a64::EmitMovReg(c, dst, src);
        ra.DefineInReg(ref, dst);
        return;
    }
    if (inst.type != Type::kI32 || IsConstRef(arg) || b.At(arg).op != Op::kTruncate) {
        throw EmitFault("CerfIR A64 lowering: extension of a value other than a truncated register");
    }
    const Type from = b.At(arg).type;
    const Ref  src  = b.At(arg).args[0];
    if ((from != Type::kI8 && from != Type::kI16) || b.TypeOf(src) != Type::kI32) {
        throw EmitFault("CerfIR A64 lowering: extension width has no A64 lowering");
    }
    const bool    byte = from == Type::kI8;
    const bool    sign = inst.op == Op::kSignExtend;
    const uint8_t s    = ra.Materialize(src);
    ra.Consume(src);
    const uint8_t d = ra.DestReg(s);
    if (byte) {
        sign ? a64::EmitSxtb(c, d, s) : a64::EmitUxtb(c, d, s);
    } else {
        sign ? a64::EmitSxth(c, d, s) : a64::EmitUxth(c, d, s);
    }
    ra.DefineInReg(ref, d);
}

void A64LowerClz(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    RequireRegisterWidth(l, ref);
    const uint8_t src = ra.Materialize(inst.args[0]);
    ra.Consume(inst.args[0]);
    const uint8_t dst = ra.DestReg(src);
    a64::EmitClz(l.Cursor(), dst, src);
    ra.DefineInReg(ref, dst);
}

}
