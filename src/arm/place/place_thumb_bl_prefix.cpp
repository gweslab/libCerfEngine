#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 10 (p. A7-27). */
uint8_t* PlaceThumbBlPrefix(uint8_t* cursor, DecodedInsn* d,
                            BlockContext* ctx) {
    using namespace x86;

    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(ArmGpr::kR14),
        ArmPcReadValue(d, ctx) + static_cast<uint32_t>(d->offset));
    return cursor;
}

}
