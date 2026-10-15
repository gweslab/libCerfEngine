#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

constexpr uint32_t kAccNone = 0u;
constexpr uint32_t kAccWord = 1u;
constexpr uint32_t kAccLong = 2u;

}

uint8_t* PlaceDualMultiply(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
    if (d->u != 0u) {
        EmitRorReg32Imm(cursor, kEcx, 16u);
    }
    EmitMovRegReg      (cursor, kEdi, kEax);
    EmitMovRegReg      (cursor, kEdx, kEcx);
    EmitMovsxReg32Reg16(cursor, kEax, kEax);
    EmitMovsxReg32Reg16(cursor, kEcx, kEcx);
    EmitImulReg32Reg32 (cursor, kEax, kEcx);
    EmitSarReg32Imm    (cursor, kEdi, 16u);
    EmitSarReg32Imm    (cursor, kEdx, 16u);
    EmitImulReg32Reg32 (cursor, kEdi, kEdx);

    EmitCdq        (cursor);
    EmitMovRegReg  (cursor, kEcx, kEdi);
    EmitSarReg32Imm(cursor, kEcx, 31u);
    if (d->n != 0u) {
        EmitSubReg32Reg32(cursor, kEax, kEdi);
        EmitSbbReg32Reg32(cursor, kEdx, kEcx);
    } else {
        EmitAddReg32Reg32(cursor, kEax, kEdi);
        EmitAdcReg32Reg32(cursor, kEdx, kEcx);
    }

    if (d->op1 == kAccLong) {
        EmitAddRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rs));
        EmitAdcRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rs), kEax);
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
        return cursor;
    }

    if (d->op1 == kAccWord) {
        EmitMovRegBaseDisp32(cursor, kEdi, kStateReg, ArmGprDisp(d->rs));
        EmitMovRegReg       (cursor, kEcx, kEdi);
        EmitSarReg32Imm     (cursor, kEcx, 31u);
        EmitAddReg32Reg32   (cursor, kEax, kEdi);
        EmitAdcReg32Reg32   (cursor, kEdx, kEcx);
    }

    const bool sets_q = !(d->op1 == kAccNone && d->n != 0u);
    if (sets_q) {
        EmitMovRegReg  (cursor, kEcx, kEax);
        EmitSarReg32Imm(cursor, kEcx, 31u);
        EmitXorRegReg  (cursor, kEcx, kEdx);
        uint8_t* in_range = EmitJzLabel(cursor);
        EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrQ);
        FixupLabel(in_range, cursor);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

}
