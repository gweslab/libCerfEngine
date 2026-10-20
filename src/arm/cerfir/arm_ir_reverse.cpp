#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;

Ref SwapAdjacentGroups(cerfir::Builder& b, Ref x, uint32_t mask, uint32_t width) {
    b.Dup(x);
    const Ref high = b.Op2(Op::kAnd, b.Op2(Op::kLsr, x, b.Const32(width)), b.Const32(mask));
    const Ref low  = b.Op2(Op::kShl, b.Op2(Op::kAnd, x, b.Const32(mask)), b.Const32(width));
    return b.Op2(Op::kOr, high, low);
}

Ref ByteReversed(ArmIrContext& c) {
    return c.b->Op1(Op::kByteSwap, ArmIrReadGpr(c, c.d->rm));
}

}

/* ARM DDI 0406C.c A8.8.145 (p. A8-563): result<31:24> = R[m]<7:0>, ...,
   result<7:0> = R[m]<31:24>. */
void ArmIrRev(ArmIrContext& c) {
    ArmIrWriteGpr(c, c.d->rd, ByteReversed(c));
}

/* A8.8.146 (p. A8-565): the two bytes of each halfword swap places. */
void ArmIrRev16(ArmIrContext& c) {
    ArmIrWriteGpr(c, c.d->rd, c.b->Op2(Op::kRor, ByteReversed(c), c.b->Const32(16u)));
}

/* A8.8.147 (p. A8-567): result<31:8> = SignExtend(R[m]<7:0>, 24);
   result<7:0> = R[m]<15:8>. */
void ArmIrRevsh(ArmIrContext& c) {
    ArmIrWriteGpr(c, c.d->rd, c.b->Op2(Op::kAsr, ByteReversed(c), c.b->Const32(16u)));
}

/* A8.8.144 (p. A8-561): result<31-i> = R[m]<i>. */
void ArmIrRbit(ArmIrContext& c) {
    cerfir::Builder& b = *c.b;
    Ref              x = ArmIrReadGpr(c, c.d->rm);
    x                  = SwapAdjacentGroups(b, x, 0x55555555u, 1u);
    x                  = SwapAdjacentGroups(b, x, 0x33333333u, 2u);
    x                  = SwapAdjacentGroups(b, x, 0x0F0F0F0Fu, 4u);
    ArmIrWriteGpr(c, c.d->rd, b.Op1(Op::kByteSwap, x));
}

}
