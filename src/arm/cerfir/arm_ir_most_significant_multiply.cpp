#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.184 SMMLA (p. A8-637), A8.8.185 SMMLS (p. A8-639),
   A8.8.186 SMMUL (p. A8-641): result = (SInt(R[a]) << 32) +/- SInt(R[n]) *
   SInt(R[m]); if round then result + 0x80000000; R[d] = result<63:32>. */
void ArmIrMostSignificantMultiply(ArmIrContext& c) {
    using cerfir::Op;
    using cerfir::Ref;
    const DecodedInsn& d      = *c.d;
    cerfir::Builder&   b      = *c.b;
    const Ref          rn     = ArmIrReadGpr(c, d.rn);
    const Ref          rm     = ArmIrReadGpr(c, d.rm);
    Ref                result = b.Op2(Op::kMulS64, rn, rm);
    if (d.n != 0u) result = b.Op1(Op::kNeg, result);
    if (d.op1 != 0u) {
        const Ref acc = b.Op2(Op::kPair, b.Const32(0u), ArmIrReadGpr(c, d.rs));
        result        = b.Op2(Op::kAdd, result, acc);
    }
    if (d.u != 0u) result = b.Op2(Op::kAdd, result, b.Const(cerfir::Type::kI64, 0x80000000u));
    const Ref high = b.Op1(Op::kHi32, result);
    b.DropHalf(result, false);
    ArmIrWriteGpr(c, d.rd, high);
}

}
