#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARM DDI 0100I A7.1.17 BL, BLX (1), H == 11 (p. A7-27). */
uint8_t* PlaceThumbBlSuffix(uint8_t* cursor, DecodedInsn* d,
                            BlockContext* ctx) {
    using namespace x86;

    constexpr int32_t kLrDisp =
        ArmGprDisp(ArmGpr::kR14);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kLrDisp);
    if (d->offset != 0) {
        EmitAddRegImm32(cursor, kEax, static_cast<uint32_t>(d->offset));
    }
    EmitMovBaseDisp32Imm32(cursor, kStateReg, kLrDisp,
                           (d->guest_address + 2u) | 1u);
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        ArmGprDisp(ArmGpr::kR15),
        kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
