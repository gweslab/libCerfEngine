#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;

uint32_t LowMask(uint32_t width) {
    return width == 32u ? 0xFFFFFFFFu : (1u << width) - 1u;
}

}

/* ARM DDI 0406C.c A8.8.19 (p. A8-337): R[d]<msbit:lsbit> = Replicate('0',
   msbit-lsbit+1); other bits of R[d] are unchanged. */
void ArmIrBfc(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    ArmIrWriteGpr(c, d.rd, c.b->Op2(Op::kAnd, ArmIrReadGpr(c, d.rd), c.b->Const32(~d.immediate)));
}

/* A8.8.20 (p. A8-339): R[d]<msbit:lsbit> = R[n]<(msbit-lsbit):0>. */
void ArmIrBfi(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    const Ref field = b.Op2(Op::kShl, b.Op2(Op::kAnd, ArmIrReadGpr(c, d.rn), b.Const32(LowMask(d.rs))),
                            b.Const32(d.op1));
    const Ref kept  = b.Op2(Op::kAnd, ArmIrReadGpr(c, d.rd), b.Const32(~d.immediate));
    ArmIrWriteGpr(c, d.rd, b.Op2(Op::kOr, kept, field));
}

/* A8.8.246 (p. A8-757): R[d] = ZeroExtend(R[n]<msbit:lsbit>, 32). */
void ArmIrUbfx(ArmIrContext& c) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    const Ref          shifted = b.Op2(Op::kLsr, ArmIrReadGpr(c, d.rn), b.Const32(d.op1));
    ArmIrWriteGpr(c, d.rd, b.Op2(Op::kAnd, shifted, b.Const32(LowMask(d.rs))));
}

/* A8.8.164 (p. A8-599): R[d] = SignExtend(R[n]<msbit:lsbit>, 32). */
void ArmIrSbfx(ArmIrContext& c) {
    const DecodedInsn& d   = *c.d;
    cerfir::Builder&   b   = *c.b;
    const Ref          top = b.Op2(Op::kShl, ArmIrReadGpr(c, d.rn), b.Const32(32u - d.op1 - d.rs));
    ArmIrWriteGpr(c, d.rd, b.Op2(Op::kAsr, top, b.Const32(32u - d.rs)));
}

}
