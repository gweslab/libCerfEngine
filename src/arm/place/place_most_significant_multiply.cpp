#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMostSignificantMultiply(uint8_t* cursor, DecodedInsn* d,
                                      BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
    EmitImulReg32       (cursor, kEcx);

    if (d->n != 0u) {
        EmitNegReg32   (cursor, kEdx);
        EmitNegReg32   (cursor, kEax);
        EmitSbbRegImm32(cursor, kEdx, 0u);
    }
    if (d->op1 != 0u) {
        EmitAddRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rs));
    }
    if (d->u != 0u) {
        EmitAddRegImm32(cursor, kEax, 0x80000000u);
        EmitAdcRegImm32(cursor, kEdx, 0u);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
    return cursor;
}

}
