#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

#include "../arm_transfer_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;

/* ARM DDI 0100I A7.1.17 BL, BLX (1) Operation (p. A7-27), H == 10:
   "LR = PC + (SignExtend(offset_11) << 12)". */
void ArmIrThumbBlPrefix(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    ArmIrWriteGpr(c, ArmGpr::kR14,
                  c.b->Const32(ArmPcReadValue(&d, c.block) + static_cast<uint32_t>(d.offset)));
}

/* ARM DDI 0100I A7.1.17 Operation (p. A7-27), H == 11: "PC = LR + (offset_11 << 1);
   LR = (address of next instruction) | 1". */
void ArmIrThumbBlSuffix(ArmIrContext& c) {
    const DecodedInsn& d  = *c.d;
    cerfir::Builder&   b  = *c.b;
    const Ref          lr = ArmIrReadGpr(c, ArmGpr::kR14);
    const Ref target = d.offset != 0 ? b.Op2(Op::kAdd, lr, b.Const32(static_cast<uint32_t>(d.offset)))
                                     : b.Snapshot(lr);
    ArmIrWriteGprConst(c, ArmGpr::kR14, (d.guest_address + 2u) | 1u);
    ArmIrWriteGpr(c, ArmGpr::kR15, target);
    ArmIrFinishPcWrite(c);
}

/* ARM DDI 0406C.c A8.8.25 BL, BLX (immediate) Operation (p. A8-349):
   "LR = PC<31:1> : '1'"; "targetAddress = Align(PC,4) + imm32" for InstrSet_ARM. */
void ArmIrThumbBlxImm(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    ArmIrWriteGprConst(c, ArmGpr::kR14, (d.guest_address + d.length) | 1u);
    ArmIrBxWritePc(
        c, b.Const32((ArmPcReadValue(&d, c.block) & ~3u) + static_cast<uint32_t>(d.offset)));
    ArmIrFinishPcWrite(c);
}

/* ARM DDI 0406C.c A8.8.29 CBNZ, CBZ Operation (p. A8-357): "if nonzero !=
   IsZero(R[n]) then BranchWritePC(PC + imm32);". */
void ArmIrCbz(ArmIrContext& c) {
    const DecodedInsn& d         = *c.d;
    cerfir::Builder&   b         = *c.b;
    const uint32_t     not_taken = b.NewLabel();
    const Ref          rn        = ArmIrReadGpr(c, d.rn);
    b.Branch(b.Op2(d.op1 != 0u ? Op::kCmpEq : Op::kCmpNe, rn, b.Const32(0u)), not_taken, true);
    ArmIrBranch(c);
    b.Label(not_taken);
}

/* ARM DDI 0406C.c A8.8.236 TBB, TBH Operation (p. A8-737):
   "BranchWritePC(PC + 2*halfwords);". */
void ArmIrTableBranch(ArmIrContext& c) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    const bool         tbh     = d.op1 != 0u;
    const uint32_t     pc      = ArmPcReadValue(&d, c.block);
    const Ref          base    = ArmIrReadGpr(c, d.rn);
    const Ref          rm      = ArmIrReadGpr(c, d.rm);
    const Ref          index   = tbh ? b.Op2(Op::kShl, rm, b.Const32(1u)) : rm;
    const Ref          address = b.Op2(Op::kAdd, base, index);
    uint32_t           attr;
    cerfir::MemAttr&   m = b.NewMemAttr(&attr);
    m.width_bytes        = tbh ? 2u : 1u;
    m.align              = ArmTableBranchAlign(d, c.block);
    ArmTransferSlowChecks(m);
    ArmIrMemOrigin(c, m);
    const Ref halfwords  = b.Load(cerfir::Type::kI32, address, cerfir::kNoRef, attr);
    ArmIrWriteGpr(c, ArmGpr::kR15,
                  b.Op2(Op::kAdd, b.Op2(Op::kShl, halfwords, b.Const32(1u)), b.Const32(pc)));
    ArmIrFinishPcWrite(c);
}

}
