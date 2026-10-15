#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* SignedSatQ to 0x7FFFFFFF / 0x80000000 + APSR.Q, bit[27] (QADD
   Operation p. A8-541, B1-1148); x86 OF = signed overflow of the result
   (SDM Vol. 2A 3-33 ADD, Vol. 2B 4-682 SUB). */
uint8_t* EmitSaturateOnOverflow(uint8_t* cursor, uint8_t reg) {
    using namespace x86;
    uint8_t* no_overflow = EmitJnoLabel(cursor);
    EmitSarReg32Imm(cursor, reg, 31u);
    EmitXorRegImm32(cursor, reg, 0x80000000u);
    EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrQ);
    FixupLabel(no_overflow, cursor);
    return cursor;
}

}

uint8_t* PlaceSaturatingArith(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    if (d->op1 >= 2u) {
        EmitAddReg32Reg32(cursor, kEcx, kEcx);
        cursor = EmitSaturateOnOverflow(cursor, kEcx);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    if ((d->op1 & 0x1u) != 0u) {
        EmitSubReg32Reg32(cursor, kEax, kEcx);
    } else {
        EmitAddReg32Reg32(cursor, kEax, kEcx);
    }
    cursor = EmitSaturateOnOverflow(cursor, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

}
