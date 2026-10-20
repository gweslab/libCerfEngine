#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

/* ARM DDI 0406C.c A8.8.233 (p. A8-731): rotated = ROR(R[m], rotation);
   R[d] = SignExtend(rotated<7:0>, 32). A8.8.230 (p. A8-725): R[d] = R[n] +
   SignExtend(rotated<7:0>, 32). */
void Extend(ArmIrContext& c, Op op, Type from) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    Ref                rotated = ArmIrReadGpr(c, d.rm);
    if (d.op1 != 0u) rotated = b.Op2(Op::kRor, rotated, b.Const32(d.op1 * 8u));
    Ref value = b.Convert(op, Type::kI32, b.Convert(Op::kTruncate, from, rotated));
    if (d.n != 0u) value = b.Op2(Op::kAdd, ArmIrReadGpr(c, d.rn), value);
    ArmIrWriteGpr(c, d.rd, value);
}

}

void ArmIrSxtb(ArmIrContext& c) { Extend(c, Op::kSignExtend, Type::kI8); }
void ArmIrSxth(ArmIrContext& c) { Extend(c, Op::kSignExtend, Type::kI16); }
void ArmIrUxtb(ArmIrContext& c) { Extend(c, Op::kZeroExtend, Type::kI8); }
void ArmIrUxth(ArmIrContext& c) { Extend(c, Op::kZeroExtend, Type::kI16); }

}
