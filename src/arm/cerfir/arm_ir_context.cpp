#include "arm_ir_context.h"

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

namespace {

int32_t FlagDisp(ArmIrFlag flag) {
    switch (flag) {
    case ArmIrFlag::kN: return ArmNfDisp();
    case ArmIrFlag::kZ: return ArmZfDisp();
    case ArmIrFlag::kC: return ArmCfDisp();
    case ArmIrFlag::kV: return ArmVfDisp();
    }
    return ArmVfDisp();
}

}

cerfir::Ref ArmIrReadPc(ArmIrContext& c) {
    return c.b->Const32(ArmPcReadValue(c.d, c.block));
}

cerfir::Ref ArmIrReadFlag(ArmIrContext& c, ArmIrFlag flag) {
    return c.b->GetSlot(cerfir::Type::kI1, static_cast<uint32_t>(FlagDisp(flag)));
}

void ArmIrWriteFlag(ArmIrContext& c, ArmIrFlag flag, cerfir::Ref value) {
    c.b->SetSlot(static_cast<uint32_t>(FlagDisp(flag)), value);
}

/* DDI 0406C.c B1.3.3 (p. B1-1148): "IT[7:0], bits[15:10, 26:25]". */
void ArmIrStoreItState(ArmIrContext& c, uint32_t itstate) {
    cerfir::Builder& b    = *c.b;
    const uint32_t   disp = static_cast<uint32_t>(ArmCpsrDisp());
    cerfir::Ref      cpsr = b.Op2(cerfir::Op::kAnd, b.GetSlot(cerfir::Type::kI32, disp),
                                  b.Const32(~kArmCpsrItMask));
    const uint32_t   bits = ArmItToCpsrBits(itstate);
    if (bits != 0u) cpsr = b.Op2(cerfir::Op::kOr, cpsr, b.Const32(bits));
    b.SetSlot(disp, cpsr);
}

}
