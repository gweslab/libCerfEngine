#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.33 (p. A8-363): R[d] = CountLeadingZeroBits(R[m]). */
void ArmIrClz(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    ArmIrWriteGpr(c, d.rd, c.b->Op1(cerfir::Op::kClz, ArmIrReadGpr(c, d.rm)));
}

}
