#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;

namespace {

void ShiftOutToT(ShIrContext& c, Op op) {
    cerfir::Builder& b  = *c.b;
    const uint32_t   rn = ShGprDisp(c.d->n);
    cerfir::FlagSet  flags;
    b.SetSlot(rn, b.Compute(op, b.ReadSlot(rn), b.Const32(1u), b.Const1(false), cerfir::kWantC, &flags));
    ShIrSetT(b, flags.c);
}

void ShiftBy(ShIrContext& c, Op op, uint32_t amount) {
    const uint32_t rn = ShGprDisp(c.d->n);
    c.b->SlotImmInto(op, rn, amount, rn, false);
}

void RotateLeft(ShIrContext& c) {
    cerfir::Builder& b  = *c.b;
    const uint32_t   rn = ShGprDisp(c.d->n);
    const Ref        x  = b.ReadSlot(rn);
    b.Dup(x);
    cerfir::FlagSet flags;
    b.ComputeOnlyFlags(Op::kShlC, x, b.Const32(1u), b.Const1(false), cerfir::kWantC, &flags);
    b.SetSlot(rn, b.Op2(Op::kRor, x, b.Const32(31u)));
    ShIrSetT(b, flags.c);
}

void RotateLeftThroughT(ShIrContext& c) {
    cerfir::Builder& b        = *c.b;
    const uint32_t   rn       = ShGprDisp(c.d->n);
    const Ref        carry_in = ShIrTAsCarry(b);
    const Ref        x        = b.ReadSlot(rn);
    b.Dup(x);
    cerfir::FlagSet flags;
    b.SetSlot(rn, b.Compute(Op::kAdc, x, x, carry_in, cerfir::kWantC, &flags));
    ShIrSetT(b, flags.c);
}

void RotateRightThroughT(ShIrContext& c) {
    cerfir::Builder& b        = *c.b;
    const uint32_t   rn       = ShGprDisp(c.d->n);
    const Ref        carry_in = ShIrTAsCarry(b);
    cerfir::FlagSet  flags;
    b.SetSlot(rn, b.Compute(Op::kRrx, b.ReadSlot(rn), carry_in, kNoRef, cerfir::kWantC, &flags));
    ShIrSetT(b, flags.c);
}

void ShiftDynamically(ShIrContext& c, Op right) {
    cerfir::Builder& b  = *c.b;
    const uint32_t   rn = ShGprDisp(c.d->n);
    const Ref        rm = b.ReadSlot(ShGprDisp(c.d->m));
    b.Dup(rm);
    b.Dup(rm);
    const Ref value = b.ReadSlot(rn);
    b.Dup(value);
    const Ref left     = b.Op2(Op::kShl, value, rm);
    const Ref partial  = b.Op2(right, value, b.Op2(Op::kXor, rm, b.Const32(0x1Fu)));
    const Ref shifted  = b.Op2(right, partial, b.Const32(1u));
    const Ref negative = b.Compare(Op::kCmpLtS, rm, b.Const32(0u));
    b.SetSlot(rn, b.Op3(Op::kSelect, negative, shifted, left));
}

}

void ShIrTranslateShift(ShIrContext& c) {
    switch (c.d->shift) {
    case ShShift::kShll:
    case ShShift::kShal:
        ShiftOutToT(c, Op::kShlC);
        return;
    case ShShift::kShlr:
        ShiftOutToT(c, Op::kLsrC);
        return;
    case ShShift::kShar:
        ShiftOutToT(c, Op::kAsrC);
        return;
    case ShShift::kRotr:
        ShiftOutToT(c, Op::kRorC);
        return;
    case ShShift::kShll2:
        ShiftBy(c, Op::kShl, 2u);
        return;
    case ShShift::kShlr2:
        ShiftBy(c, Op::kLsr, 2u);
        return;
    case ShShift::kShll8:
        ShiftBy(c, Op::kShl, 8u);
        return;
    case ShShift::kShlr8:
        ShiftBy(c, Op::kLsr, 8u);
        return;
    case ShShift::kShll16:
        ShiftBy(c, Op::kShl, 16u);
        return;
    case ShShift::kShlr16:
        ShiftBy(c, Op::kLsr, 16u);
        return;
    case ShShift::kRotl:
        RotateLeft(c);
        return;
    case ShShift::kRotcl:
        RotateLeftThroughT(c);
        return;
    case ShShift::kRotcr:
        RotateRightThroughT(c);
        return;
    case ShShift::kShad:
        ShiftDynamically(c, Op::kAsr);
        return;
    case ShShift::kShld:
        ShiftDynamically(c, Op::kLsr);
        return;
    }
}

}
