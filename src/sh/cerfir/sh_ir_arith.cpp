#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

Ref ZeroAsCarry(cerfir::Builder& b, Ref value) {
    cerfir::FlagSet flags;
    b.ComputeOnlyFlags(Op::kSub, b.Const32(0u), value, kNoRef, cerfir::kWantC, &flags);
    return flags.c;
}

Ref TClearAsCarry(cerfir::Builder& b) {
    return ZeroAsCarry(b, b.Op2(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(kShSrT)));
}

void ResultAndT(ShIrContext& c, Op op, Ref a, Ref b_arg, Ref carry_in, uint8_t want, bool t_inverted) {
    cerfir::Builder& b = *c.b;
    cerfir::FlagSet  flags;
    const Ref        value = b.Compute(op, a, b_arg, carry_in, want, &flags);
    b.SetSlot(ShGprDisp(c.d->n), value);
    const Ref t = want == cerfir::kWantC ? flags.c : want == cerfir::kWantV ? flags.v : flags.z;
    if (t_inverted) {
        ShIrSetNotT(b, t);
    } else {
        ShIrSetT(b, t);
    }
}

void WithCarryIn(ShIrContext& c, Op op, Ref a, Ref carry_in, bool t_inverted) {
    ResultAndT(c, op, a, c.b->ReadSlot(ShGprDisp(c.d->m)), carry_in, cerfir::kWantC, t_inverted);
}

void Extend(ShIrContext& c, Op op, Type from) {
    cerfir::Builder& b = *c.b;
    b.SetSlot(ShGprDisp(c.d->n), ShIrExtend(b, op, from, b.ReadSlot(ShGprDisp(c.d->m))));
}

void Div0s(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    const Ref            q = b.Op2(Op::kLsr, b.ReadSlot(ShGprDisp(d.n)), b.Const32(31u));
    const Ref            m = b.Op2(Op::kLsr, b.ReadSlot(ShGprDisp(d.m)), b.Const32(31u));
    b.Dup(q);
    b.Dup(m);
    const Ref t      = b.Op2(Op::kXor, q, m);
    const Ref q_bit  = b.Op2(Op::kShl, q, b.Const32(kShSrQShift));
    const Ref m_bit  = b.Op2(Op::kShl, m, b.Const32(kShSrQShift + 1u));
    const Ref kept   = b.Op2(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(~(kShSrM | kShSrQ | kShSrT)));
    const Ref with_q = b.Op2(Op::kOr, kept, q_bit);
    const Ref with_m = b.Op2(Op::kOr, with_q, m_bit);
    b.SetSlot(ShSrDisp(), b.Op2(Op::kOr, with_m, t));
}

void Div1(ShIrContext& c) {
    cerfir::Builder&     b  = *c.b;
    const ShDecodedInsn& d  = *c.d;
    const Ref            sr = b.ReadSlot(ShSrDisp());
    const Ref            rn = b.ReadSlot(ShGprDisp(d.n));

    const Ref m_at_q  = b.Op2(Op::kLsr, sr, b.Const32(1u));
    const Ref q_m     = b.Op2(Op::kXor, sr, m_at_q);
    const Ref q_m_low = b.Op2(Op::kLsr, q_m, b.Const32(kShSrQShift));
    const Ref q_ne_m  = b.Op2(Op::kAnd, q_m_low, b.Const32(1u));
    b.Dup(q_ne_m);
    b.Dup(q_ne_m);

    const Ref shifted_rn = b.Op2(Op::kShl, rn, b.Const32(1u));
    const Ref old_t      = b.Op2(Op::kAnd, sr, b.Const32(kShSrT));
    const Ref dividend   = b.Op2(Op::kOr, shifted_rn, old_t);
    const Ref negate     = b.Op1(Op::kNeg, q_ne_m);
    const Ref divisor    = b.Op2(Op::kXor, b.ReadSlot(ShGprDisp(d.m)), negate);
    const Ref rn_msb     = b.Op2(Op::kLsr, rn, b.Const32(31u));

    const Ref       subtract  = ZeroAsCarry(b, q_ne_m);
    cerfir::FlagSet carry;
    const Ref       result    = b.Compute(Op::kSbc, dividend, divisor, subtract, cerfir::kWantC, &carry);
    const Ref       carry_bit = b.Convert(Op::kZeroExtend, Type::kI32, carry.c);
    const Ref       e         = b.Op2(Op::kXor, rn_msb, carry_bit);
    b.SetSlot(ShGprDisp(d.n), result);

    b.Dup(e);
    const Ref t_new   = b.Op2(Op::kXor, e, q_ne_m);
    const Ref e_at_q  = b.Op2(Op::kShl, e, b.Const32(kShSrQShift));
    const Ref q_xor_e = b.Op2(Op::kXor, sr, e_at_q);
    const Ref q_raw   = b.Op2(Op::kAnd, q_xor_e, b.Const32(kShSrQ));
    const Ref q_new   = b.Op2(Op::kXor, q_raw, b.Const32(kShSrQ));
    const Ref kept    = b.Op2(Op::kAnd, sr, b.Const32(~(kShSrQ | kShSrT)));
    const Ref with_q  = b.Op2(Op::kOr, kept, q_new);
    b.SetSlot(ShSrDisp(), b.Op2(Op::kOr, with_q, t_new));
}

}

void ShIrTranslateArith(ShIrContext& c) {
    cerfir::Builder&     b  = *c.b;
    const ShDecodedInsn& d  = *c.d;
    const uint32_t       rn = ShGprDisp(d.n);
    const uint32_t       rm = ShGprDisp(d.m);
    switch (d.arith) {
    case ShArith::kAdd:
        b.SlotSlotInto(Op::kAdd, rn, rm, rn, false);
        return;
    case ShArith::kSub:
        b.SlotSlotInto(Op::kSub, rn, rm, rn, false);
        return;
    case ShArith::kAddc: {
        const Ref carry_in = ShIrTAsCarry(b);
        WithCarryIn(c, Op::kAdc, b.ReadSlot(rn), carry_in, false);
        return;
    }
    case ShArith::kSubc: {
        const Ref no_borrow_in = TClearAsCarry(b);
        WithCarryIn(c, Op::kSbc, b.ReadSlot(rn), no_borrow_in, true);
        return;
    }
    case ShArith::kNegc: {
        const Ref no_borrow_in = TClearAsCarry(b);
        WithCarryIn(c, Op::kSbc, b.Const32(0u), no_borrow_in, true);
        return;
    }
    case ShArith::kAddv:
        ResultAndT(c, Op::kAdd, b.ReadSlot(rn), b.ReadSlot(rm), kNoRef, cerfir::kWantV, false);
        return;
    case ShArith::kSubv:
        ResultAndT(c, Op::kSub, b.ReadSlot(rn), b.ReadSlot(rm), kNoRef, cerfir::kWantV, false);
        return;
    case ShArith::kDt:
        ResultAndT(c, Op::kSub, b.ReadSlot(rn), b.Const32(1u), kNoRef, cerfir::kWantZ, false);
        return;
    case ShArith::kNeg:
        b.SetSlot(rn, b.Op1(Op::kNeg, b.ReadSlot(rm)));
        return;
    case ShArith::kDiv0s:
        Div0s(c);
        return;
    case ShArith::kDiv0u:
        b.SlotImmInto(Op::kAnd, ShSrDisp(), ~(kShSrM | kShSrQ | kShSrT), ShSrDisp(), false);
        return;
    case ShArith::kDiv1:
        Div1(c);
        return;
    case ShArith::kExtsB:
        Extend(c, Op::kSignExtend, Type::kI8);
        return;
    case ShArith::kExtsW:
        Extend(c, Op::kSignExtend, Type::kI16);
        return;
    case ShArith::kExtuB:
        Extend(c, Op::kZeroExtend, Type::kI8);
        return;
    case ShArith::kExtuW:
        Extend(c, Op::kZeroExtend, Type::kI16);
        return;
    }
}

Ref ShIrExtend(cerfir::Builder& b, Op op, Type from, Ref value) {
    return b.Convert(op, Type::kI32, b.Convert(Op::kTruncate, from, value));
}

void ShIrTranslateAddImm(ShIrContext& c) {
    const uint32_t rn = ShGprDisp(c.d->n);
    c.b->SlotImmInto(Op::kAdd, rn, c.d->disp, rn, false);
}

}
