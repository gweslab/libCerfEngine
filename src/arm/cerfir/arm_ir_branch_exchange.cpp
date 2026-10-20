#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

/* ARM DDI 0406C.c A8.8.26 BLX (register) (p. A8-351): target = R[m] before
   LR = PC - 4 (ARM) or (PC - 2)<31:1>:'1' (Thumb); A8.8.27 BX (p. A8-353). */
void BranchExchange(ArmIrContext& c, bool link) {
    const DecodedInsn& d      = *c.d;
    cerfir::Builder&   b      = *c.b;
    const cerfir::Ref  read   = ArmIrReadGpr(c, d.rm);
    const cerfir::Ref  target = link ? b.Snapshot(read) : read;
    if (link) {
        ArmIrWriteGpr(c, ArmGpr::kR14,
                      b.Const32(c.block->thumb ? ((d.guest_address + 2u) | 1u)
                                               : (d.guest_address + 4u)));
    }
    ArmIrBxWritePc(c, target);
    ArmIrFinishPcWrite(c);
}

}

void ArmIrBx(ArmIrContext& c) {
    BranchExchange(c, false);
}

void ArmIrBlxReg(ArmIrContext& c) {
    BranchExchange(c, true);
}

/* A8.8.25 BLX (immediate) A2 (pp. A8-348/349): LR = PC - 4, target
   PC + SignExtend(imm24:H:'0') in Thumb state. */
void ArmIrArmBlxImm(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    ArmIrWriteGprConst(c, ArmGpr::kR14, d.guest_address + d.length);
    const uint32_t target = (ArmPcReadValue(&d, c.block) + static_cast<uint32_t>(d.offset)) | 1u;
    ArmIrBxWritePc(c, b.Const32(target));
    ArmIrFinishPcWrite(c);
}

}
