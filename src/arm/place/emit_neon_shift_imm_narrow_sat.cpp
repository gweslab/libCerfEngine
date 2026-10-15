#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_sat.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* Saturating narrowing right shift (A8.8.381 VQSHRN/VQSHRUN truncating,
   A8.8.378 VQRSHRN/VQRSHRUN rounding). */
uint8_t* PlaceNeonShiftImmNarrowSat(uint8_t*      cursor,
                                    DecodedInsn*  d,
                                    BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t op    = d->op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t L_bit = (w >> 7)  & 1u;
    const uint32_t imm6  = (w >> 16) & 0x3Fu;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* ARM DDI 0406C.c A8.8.381 (p. A8-1018) and A8.8.378 (p. A8-1012) encoding
       T1/A1: bit[7] is 0 and bit[6] is 0 for VQSHR{U}N, 1 for VQRSHR{U}N; both
       state "if Vm<0> == `1' then UNDEFINED". */
    if (L_bit != 0u || (m_idx & 1u) != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint32_t esize, shift_amount;
    if (imm6 & 0x20u) {
        esize        = 32u;
        shift_amount = 64u - imm6;
    } else if (imm6 & 0x10u) {
        esize        = 16u;
        shift_amount = 32u - imm6;
    } else if (imm6 & 0x08u) {
        esize        = 8u;
        shift_amount = 16u - imm6;
    } else {
        emit->Owner().Panic("PlaceNeonShiftImmNarrowSat: imm6=000xxx word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }

    EmitPush32(cursor, shift_amount);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->NeonSat())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeonSat::HandleShiftImmNarrowSatHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
