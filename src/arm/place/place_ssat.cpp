#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceSsat(uint8_t* cursor, DecodedInsn* d, BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));

    if (d->op1 == kSrLsl) {
        if (d->rs != 0u) {
            EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        }
    } else {
        EmitSarReg32Imm(cursor, kEax,
                        static_cast<uint8_t>(d->rs >= 32u ? 31u : d->rs));
    }

    if (d->immediate < 32u) {
        const uint32_t bound = 1u << (d->immediate - 1u);
        const uint32_t hi    = bound - 1u;
        const uint32_t lo    = 0u - bound;

        EmitCmpRegImm32(cursor, kEax, hi);
        uint8_t* below_hi = EmitJleLabel(cursor);
        EmitMovRegImm32      (cursor, kEax, hi);
        EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrQ);
        FixupLabel(below_hi, cursor);

        EmitCmpRegImm32(cursor, kEax, lo);
        uint8_t* above_lo = EmitJgeLabel(cursor);
        EmitMovRegImm32      (cursor, kEax, lo);
        EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrQ);
        FixupLabel(above_lo, cursor);
    }

    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

}
