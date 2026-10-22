#include <utility>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

uint8_t CompareCond(Op op, bool swapped) {
    switch (op) {
    case Op::kCmpEq:  return a64::kEq;
    case Op::kCmpNe:  return a64::kNe;
    case Op::kCmpLtS: return swapped ? a64::kGt : a64::kLt;
    case Op::kCmpLtU: return swapped ? a64::kHi : a64::kCc;
    case Op::kCmpLeS: return swapped ? a64::kGe : a64::kLe;
    default:          throw EmitFault("CerfIR A64 lowering: not a compare op");
    }
}

void EmitCompareOperand(A64Lowering& l, uint8_t reg, Ref b) {
    A64RegAlloc& ra = l.Regs();
    uint8_t*&    c  = l.Cursor();
    if (IsConstRef(b)) {
        const uint32_t v = ra.ConstOf(b);
        if (a64::AddSubImmFits(v)) {
            a64::EmitCmpImm(c, reg, v);
            return;
        }
        if (a64::AddSubImmFits(0u - v)) {
            a64::EmitAddsImm(c, a64::kZr, reg, 0u - v);
            return;
        }
    }
    a64::EmitCmpReg(c, reg, ra.Materialize(b));
}

void LowerNarrowCompare(A64Lowering& l, Ref ref) {
    const Inst&  inst    = l.GetBlock().At(ref);
    A64RegAlloc& ra      = l.Regs();
    Ref          a       = inst.args[0];
    Ref          b       = inst.args[1];
    bool         swapped = false;
    if (IsConstRef(a)) {
        std::swap(a, b);
        swapped = true;
    }
    ra.BeforeFlagsClobber();
    EmitCompareOperand(l, ra.Materialize(a), b);
    ra.Consume(a);
    ra.Consume(b);
    l.ClearFlagSource();
    ra.DefineFlags(ref, CompareCond(inst.op, swapped));
}

void LowerWideCompare(A64Lowering& l, Ref ref) {
    const Inst&  inst = l.GetBlock().At(ref);
    A64RegAlloc& ra   = l.Regs();
    uint8_t*&    c    = l.Cursor();
    const Op     op   = inst.op;
    const bool   le   = op == Op::kCmpLeS;
    const Ref    x    = le ? inst.args[1] : inst.args[0];
    const Ref    y    = le ? inst.args[0] : inst.args[1];
    ra.BeforeFlagsClobber();

    const A64WideHalf x_lo = l.Half(x, false);
    const A64WideHalf x_hi = l.Half(x, true);
    const A64WideHalf y_lo = l.Half(y, false);
    const A64WideHalf y_hi = l.Half(y, true);
    const uint8_t     xl   = A64HalfReg(l, x_lo, false);
    const uint8_t     xh   = A64HalfReg(l, x_hi, true);
    const uint8_t     yl   = A64HalfReg(l, y_lo, true);
    const uint8_t     yh   = A64HalfReg(l, y_hi, true);
    a64::EmitCmpReg(c, xl, yl);
    uint8_t cond = 0u;
    if (op == Op::kCmpEq || op == Op::kCmpNe) {
        a64::EmitCcmpReg(c, xh, yh, 0u, a64::kEq);
        cond = op == Op::kCmpEq ? a64::kEq : a64::kNe;
    } else {
        a64::EmitSbcs(c, a64::kZr, xh, yh);
        cond = op == Op::kCmpLtU ? a64::kCc : (le ? a64::kGe : a64::kLt);
    }
    A64ReleaseHalf(l, x_lo, xl);
    A64ReleaseHalf(l, x_hi, xh);
    A64ReleaseHalf(l, y_lo, yl);
    A64ReleaseHalf(l, y_hi, yh);
    l.ClearFlagSource();
    ra.DefineFlags(ref, cond);
}

}

void A64LowerCompare(A64Lowering& l, Ref ref) {
    const Block& b = l.GetBlock();
    const Type   t = b.TypeOf(b.At(ref).args[0]);
    if (t == Type::kI64) {
        LowerWideCompare(l, ref);
        return;
    }
    if (t != Type::kI32) {
        throw EmitFault("CerfIR A64 lowering: compare of a type other than i32 or i64");
    }
    LowerNarrowCompare(l, ref);
}

}
