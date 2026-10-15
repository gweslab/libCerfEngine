#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* SMLA<x><y> / SMLAW<y> Operation (DDI 0406C.c pp. A8-621 / A8-631):
   signed overflow of the accumulate sets APSR.Q, bit[27] (B1-1148). */
uint8_t* EmitQOnOverflow(uint8_t* cursor) {
    using namespace x86;
    uint8_t* no_overflow = EmitJnoLabel(cursor);
    EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrQ);
    FixupLabel(no_overflow, cursor);
    return cursor;
}

}

uint8_t* PlaceHalfwordMultiply(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;
    const uint32_t row = d->op1;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    if (row != 1u) {
        if (d->n != 0u) {
            EmitSarReg32Imm(cursor, kEax, 16u);
        } else {
            EmitMovsxReg32Reg16(cursor, kEax, kEax);
        }
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
    if (d->u != 0u) {
        EmitSarReg32Imm(cursor, kEcx, 16u);
    } else {
        EmitMovsxReg32Reg16(cursor, kEcx, kEcx);
    }

    switch (row) {
    case 0u:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rs));
        cursor = EmitQOnOverflow(cursor);
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
        return cursor;
    case 1u:
        EmitImulReg32(cursor, kEcx);
        EmitShrdReg32Reg32Imm8(cursor, kEax, kEdx, 16u);
        if (d->s == 0u) {
            EmitAddRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rs));
            cursor = EmitQOnOverflow(cursor);
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
        return cursor;
    case 2u:
        EmitImulReg32(cursor, kEcx);
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rs));
        EmitAdcRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rs), kEax);
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
        return cursor;
    default:
        EmitImulReg32Reg32(cursor, kEax, kEcx);
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
        return cursor;
    }
}

}
