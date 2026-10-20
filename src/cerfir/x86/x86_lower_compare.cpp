#include <utility>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using Operand     = X86RegAlloc::Operand;
using OperandKind = X86RegAlloc::OperandKind;

uint8_t CompareCond(Op op, bool swapped) {
    switch (op) {
    case Op::kCmpEq:  return kX86CondZ;
    case Op::kCmpNe:  return kX86CondNz;
    case Op::kCmpLtS: return swapped ? kX86CondG : kX86CondL;
    case Op::kCmpLtU: return swapped ? kX86CondA : kX86CondC;
    case Op::kCmpLeS: return swapped ? kX86CondGe : kX86CondLe;
    default:          throw EmitFault("CerfIR x86 lowering: not a compare op");
    }
}

void EmitCompareOperand(uint8_t*& c, uint8_t reg, const Operand& o) {
    switch (o.kind) {
    case OperandKind::kReg:
        x86::EmitCmpReg32Reg32(c, reg, o.reg);
        break;
    case OperandKind::kSlot:
        x86::EmitCmpRegBaseDisp32(c, reg, x86::kStateReg, o.disp);
        break;
    case OperandKind::kImm:
        x86::EmitCmpRegImm32(c, reg, o.imm);
        break;
    }
}

void LowerNarrowCompare(X86Lowering& l, Ref ref) {
    const Inst&  inst    = l.GetBlock().At(ref);
    X86RegAlloc& ra      = l.Regs();
    Ref          a       = inst.args[0];
    Ref          b       = inst.args[1];
    bool         swapped = false;
    if (IsConstRef(a)) {
        std::swap(a, b);
        swapped = true;
    }
    ra.BeforeFlagsClobber();
    const uint8_t reg = ra.MaterializeWide(a, kX86AllocRegs);
    EmitCompareOperand(l.Cursor(), reg, ra.Rm(b));
    ra.Consume(a);
    ra.Consume(b);
    l.ClearFlagSource();
    ra.DefineFlags(ref, CompareCond(inst.op, swapped));
}

void LowerWideCompare(X86Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    X86RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Op     op   = inst.op;
    const bool   le   = op == Op::kCmpLeS;
    const Ref    x    = le ? inst.args[1] : inst.args[0];
    const Ref    y    = le ? inst.args[0] : inst.args[1];
    ra.BeforeFlagsClobber();

    const X86WideHalf x_lo = l.Half(x, false);
    const X86WideHalf x_hi = l.Half(x, true);
    const X86WideHalf y_lo = l.Half(y, false);
    const X86WideHalf y_hi = l.Half(y, true);
    const uint8_t     lo   = X86TakeHalf(l, x_lo, ref);
    const uint8_t     hi   = X86TakeHalf(l, x_hi, ref);
    const Operand     o_lo = X86HalfOperand(ra, y_lo);
    const Operand     o_hi = X86HalfOperand(ra, y_hi);
    X86EmitAluOperand(c, Op::kSub, lo, o_lo);
    uint8_t cond = 0u;
    if (op == Op::kCmpEq || op == Op::kCmpNe) {
        X86EmitAluOperand(c, Op::kSub, hi, o_hi);
        x86::EmitOrReg32Reg32(c, lo, hi);
        cond = op == Op::kCmpEq ? kX86CondZ : kX86CondNz;
    } else {
        X86EmitAluOperand(c, Op::kSbc, hi, o_hi);
        cond = op == Op::kCmpLtU ? kX86CondC : (le ? kX86CondGe : kX86CondL);
    }
    ra.ReleaseTemp(lo);
    ra.ReleaseTemp(hi);
    if (!y_lo.imm) ra.Consume(y_lo.ref);
    if (!y_hi.imm) ra.Consume(y_hi.ref);
    l.ClearFlagSource();
    ra.DefineFlags(ref, cond);
}

}

void X86LowerCompare(X86Lowering& l, Ref ref) {
    const Block& b = l.GetBlock();
    const Type   t = b.TypeOf(b.At(ref).args[0]);
    if (t == Type::kI64) {
        LowerWideCompare(l, ref);
        return;
    }
    if (t != Type::kI32) {
        throw EmitFault("CerfIR x86 lowering: compare of a type other than i32 or i64");
    }
    LowerNarrowCompare(l, ref);
}

}
