#include <cstddef>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceArmBlxImm(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(ArmGpr::kR14),
        d->guest_address + d->length);
    cursor = EmitArmInterworkingPcImm32(cursor,
        (ArmPcReadValue(d, ctx) + static_cast<uint32_t>(d->offset)) | 1u);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
