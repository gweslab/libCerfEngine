#include <cerfengine/arm/cpu_state.h>

#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.134 QADD (p. A8-541), A8.8.138 QDADD (p. A8-549),
   A8.8.139 QDSUB (p. A8-551), A8.8.141 QSUB (p. A8-555): doubled =
   SignedSatQ(2 * SInt(R[n]), 32); R[d] = SignedSatQ(SInt(R[m]) +/- doubled, 32). */
void ArmIrSaturatingArith(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    const uint32_t     cpsr = static_cast<uint32_t>(ArmCpsrDisp());
    cerfir::Ref        rn   = ArmIrReadGpr(c, d.rn);
    if (d.op1 >= 2u) rn = b.Saturate(cerfir::Op::kSatAdd, rn, rn, cpsr, kArmCpsrQ);
    const cerfir::Op op = (d.op1 & 1u) != 0u ? cerfir::Op::kSatSub : cerfir::Op::kSatAdd;
    ArmIrWriteGpr(c, d.rd, b.Saturate(op, ArmIrReadGpr(c, d.rm), rn, cpsr, kArmCpsrQ));
}

}
