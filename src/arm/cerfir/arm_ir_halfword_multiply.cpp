#include <cerfengine/arm/cpu_state.h>

#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;

Ref AccumulateSettingQ(ArmIrContext& c, Ref value, uint32_t ra) {
    cerfir::Builder& b   = *c.b;
    const Ref        acc = ArmIrReadGpr(c, ra);
    cerfir::FlagSet  flags;
    const Ref sum = b.Compute(Op::kAdd, value, acc, cerfir::kNoRef, cerfir::kWantV, &flags);
    b.OrSticky(flags.v, static_cast<uint32_t>(ArmCpsrDisp()), kArmCpsrQ);
    return sum;
}

}

Ref ArmIrSignedHalf(ArmIrContext& c, Ref value, bool top) {
    cerfir::Builder& b = *c.b;
    if (top) return b.Op2(Op::kAsr, value, b.Const32(16u));
    const Ref low = b.Convert(Op::kTruncate, cerfir::Type::kI16, value);
    return b.Convert(Op::kSignExtend, cerfir::Type::kI32, low);
}

/* ARM DDI 0406C.c A8.8.176 SMLA<x><y> (p. A8-621), A8.8.181 SMLAW<y> (p. A8-631),
   A8.8.190 SMULW<y> (p. A8-649), A8.8.179 SMLAL<x><y> (p. A8-627), A8.8.188
   SMUL<x><y> (p. A8-645). */
void ArmIrHalfwordMultiply(ArmIrContext& c) {
    const DecodedInsn& d   = *c.d;
    cerfir::Builder&   b   = *c.b;
    const Ref          rn  = ArmIrReadGpr(c, d.rn);
    const Ref          op1 = d.op1 == 1u ? rn : ArmIrSignedHalf(c, rn, d.n != 0u);
    const Ref          rm  = ArmIrReadGpr(c, d.rm);
    const Ref          op2 = ArmIrSignedHalf(c, rm, d.u != 0u);
    switch (d.op1) {
    case 0u: {
        const Ref product = b.Op2(Op::kMul, op1, op2);
        ArmIrWriteGpr(c, d.rd, AccumulateSettingQ(c, product, d.rs));
        return;
    }
    case 1u: {
        const Ref product = b.Op2(Op::kMulS64, op1, op2);
        const Ref shifted = b.Op2(Op::kAsr, product, b.Const32(16u));
        const Ref middle  = b.Op1(Op::kLo32, shifted);
        b.DropHalf(shifted, true);
        ArmIrWriteGpr(c, d.rd, d.s != 0u ? middle : AccumulateSettingQ(c, middle, d.rs));
        return;
    }
    case 2u: {
        const Ref product = b.Op2(Op::kMulS64, op1, op2);
        const Ref lo      = ArmIrReadGpr(c, d.rs);
        const Ref hi      = ArmIrReadGpr(c, d.rd);
        const Ref acc     = b.Op2(Op::kPair, lo, hi);
        const Ref sum     = b.Op2(Op::kAdd, product, acc);
        ArmIrWriteGpr(c, d.rs, b.Op1(Op::kLo32, sum));
        ArmIrWriteGpr(c, d.rd, b.Op1(Op::kHi32, sum));
        return;
    }
    default:
        ArmIrWriteGpr(c, d.rd, b.Op2(Op::kMul, op1, op2));
        return;
    }
}

}
