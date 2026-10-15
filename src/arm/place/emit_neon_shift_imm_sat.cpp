#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_sat.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VQSHL imm / VQSHLU (A8.8.380): saturating shift left by immediate.
   Same L:imm6 case table as VSHL imm (left-shift formula, range
   [0, esize-1]). */
uint8_t* PlaceNeonShiftImmSat(uint8_t*      cursor,
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
    const uint32_t Q     = (w >> 6)  & 1u;
    const uint32_t L     = (w >> 7)  & 1u;
    const uint32_t imm6  = (w >> 16) & 0x3Fu;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint32_t esize, shift_amount;
    if (L != 0u) {
        esize        = 64u;
        shift_amount = imm6;
    } else if (imm6 & 0x20u) {
        esize        = 32u;
        shift_amount = imm6 - 32u;
    } else if (imm6 & 0x10u) {
        esize        = 16u;
        shift_amount = imm6 - 16u;
    } else if (imm6 & 0x08u) {
        esize        = 8u;
        shift_amount = imm6 - 8u;
    } else {
        emit->Owner().Panic("PlaceNeonShiftImmSat: L:imm6=0000xxx word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, shift_amount);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->NeonSat())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeonSat::HandleShiftImmSatHelper));
    EmitAddRegImm32(cursor, kEsp, 28);
    return cursor;
}

}
