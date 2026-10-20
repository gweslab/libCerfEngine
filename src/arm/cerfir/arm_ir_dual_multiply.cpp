#include <cerfengine/arm/cpu_state.h>

#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;

constexpr uint32_t kAccNone = 0u;
constexpr uint32_t kAccWord = 1u;
constexpr uint32_t kAccLong = 2u;

Ref SignExtended64(cerfir::Builder& b, Ref value) {
    b.Dup(value);
    const Ref sign = b.Op2(Op::kAsr, value, b.Const32(31u));
    return b.Op2(Op::kPair, value, sign);
}

}

/* ARM DDI 0406C.c A8.8.177 SMLAD (p. A8-623), A8.8.180 SMLALD (p. A8-629),
   A8.8.182 SMLSD (p. A8-633), A8.8.183 SMLSLD (p. A8-635), A8.8.187 SMUAD
   (p. A8-643), A8.8.191 SMUSD (p. A8-651). */
void ArmIrDualMultiply(ArmIrContext& c) {
    const DecodedInsn& d  = *c.d;
    cerfir::Builder&   b  = *c.b;
    const Ref          rn = ArmIrReadGpr(c, d.rn);
    Ref                rm = ArmIrReadGpr(c, d.rm);
    if (d.u != 0u) {
        rm = b.Op2(Op::kRor, rm, b.Const32(16u));
        b.Dup(rm);
    }

    const Ref n_lo   = ArmIrSignedHalf(c, rn, false);
    const Ref m_lo   = ArmIrSignedHalf(c, rm, false);
    const Ref first  = b.Op2(Op::kMul, n_lo, m_lo);
    const Ref n_hi   = ArmIrSignedHalf(c, rn, true);
    const Ref m_hi   = ArmIrSignedHalf(c, rm, true);
    const Ref second = b.Op2(Op::kMul, n_hi, m_hi);
    const Ref wide1  = SignExtended64(b, first);
    const Ref wide2  = SignExtended64(b, second);
    Ref       result = b.Op2(d.n != 0u ? Op::kSub : Op::kAdd, wide1, wide2);

    if (d.op1 == kAccLong) {
        const Ref lo  = ArmIrReadGpr(c, d.rs);
        const Ref hi  = ArmIrReadGpr(c, d.rd);
        const Ref acc = b.Op2(Op::kPair, lo, hi);
        result        = b.Op2(Op::kAdd, result, acc);
        ArmIrWriteGpr(c, d.rs, b.Op1(Op::kLo32, result));
        ArmIrWriteGpr(c, d.rd, b.Op1(Op::kHi32, result));
        return;
    }
    if (d.op1 == kAccWord) {
        const Ref acc = SignExtended64(b, ArmIrReadGpr(c, d.rs));
        result        = b.Op2(Op::kAdd, result, acc);
    }
    const Ref low = b.Op1(Op::kLo32, result);
    if (d.op1 != kAccNone || d.n == 0u) {
        b.Dup(low);
        const Ref sign     = b.Op2(Op::kAsr, low, b.Const32(31u));
        const Ref high     = b.Op1(Op::kHi32, result);
        const Ref overflow = b.Op2(Op::kCmpNe, sign, high);
        b.OrSticky(overflow, static_cast<uint32_t>(ArmCpsrDisp()), kArmCpsrQ);
    } else {
        b.DropHalf(result, true);
    }
    ArmIrWriteGpr(c, d.rd, low);
}

}
