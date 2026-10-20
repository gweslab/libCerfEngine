#include <cerfengine/arm/cpu_state.h>

#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.193 SSAT (p. A8-653): operand = Shift(R[n], shift_t, shift_n,
   APSR.C); (result, sat) = SignedSatQ(SInt(operand), saturate_to); "if sat then
   APSR.Q = '1'". */
void ArmIrSsat(ArmIrContext& c) {
    using cerfir::Op;
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    cerfir::Ref        operand = ArmIrReadGpr(c, d.rn);
    if (d.op1 == kSrLsl) {
        operand = b.Op2(Op::kShl, operand, b.Const32(d.rs));
    } else {
        operand = b.Op2(Op::kAsr, operand, b.Const32(d.rs >= 32u ? 31u : d.rs));
    }
    if (d.immediate < 32u) {
        operand = b.Saturate(Op::kSignedSat, operand, b.Const32(d.immediate),
                             static_cast<uint32_t>(ArmCpsrDisp()), kArmCpsrQ);
    }
    ArmIrWriteGpr(c, d.rd, operand);
}

}
