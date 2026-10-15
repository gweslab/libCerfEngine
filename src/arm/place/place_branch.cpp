#include <cstddef>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.18 B (p. A8-335): BranchWritePC(PC + imm32);
   A8.8.25 BL Operation (p. A8-349): LR = PC - 4 in ARM state and
   PC<31:1>:'1' otherwise, targetAddress = PC + imm32. */
uint8_t* PlaceBranch(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    if (d->l != 0u) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg, ArmGprDisp(ArmGpr::kR14),
            ctx->thumb ? ((d->guest_address + d->length) | 1u)
                       :  (d->guest_address + 4u));
    }
    const uint32_t target =
        ArmPcReadValue(d, ctx) + static_cast<uint32_t>(d->offset);
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(15u),
        target);

    cursor = EmitChainToBlock(cursor, ctx, target, 0u);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
