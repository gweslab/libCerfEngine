#include <cstddef>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.25 BL, BLX (immediate) Operation (p. A8-349):
   LR = PC<31:1>:'1'; targetAddress = Align(PC,4) + imm32 when targetInstrSet
   is InstrSet_ARM; SelectInstrSet then BranchWritePC. */
uint8_t* PlaceThumbBlxImm(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(ArmGpr::kR14),
        (d->guest_address + d->length) | 1u);
    cursor = EmitArmInterworkingPcImm32(cursor,
        (ArmPcReadValue(d, ctx) & ~3u) + static_cast<uint32_t>(d->offset));
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
