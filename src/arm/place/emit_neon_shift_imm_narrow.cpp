#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_shift_imm.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* Narrowing right shift family: VSHRN (A8.8.399) truncating, VRSHRN
   (A8.8.390) rounding. */
uint8_t* PlaceNeonShiftImmNarrow(uint8_t*      cursor,
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

    /* ARM DDI 0406C.c A8.8.399 (p. A8-1054) and A8.8.390 (p. A8-1036) encoding
       T1/A1: bit[7] is 0 and bit[6] is 0 for VSHRN, 1 for VRSHRN; both state
       "if Vm<0> == `1' then UNDEFINED". */
    if (L_bit != 0u || (m_idx & 1u) != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* ARM DDI 0406C.c A8.8.399 (p. A8-1054) and A8.8.390 (p. A8-1036) imm6
       case table. */
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
        /* ARM DDI 0406C.c A8.8.399 (p. A8-1054) and A8.8.390 (p. A8-1036):
           "if imm6 IN "000xxx" then SEE "Related encodings"" - One register and
           a modified immediate value (p. A7-269). */
        emit->Owner().Panic("PlaceNeonShiftImmNarrow: imm6=000xxx word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }

    EmitPush32(cursor, shift_amount);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->NeonShiftImm())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeonShiftImm::HandleShiftImmNarrowHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
