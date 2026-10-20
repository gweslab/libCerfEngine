#include "../arm_emit_services.h"
#include "../arm_neon_sat.h"
#include "plan_fns.h"

namespace cerfengine {

/* Saturating narrowing right shift (A8.8.381 VQSHRN/VQSHRUN truncating,
   A8.8.378 VQRSHRN/VQRSHRUN rounding). */
ArmCallPlan ArmPlanNeonShiftImmNarrowSat(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
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
    if (L_bit != 0u || (m_idx & 1u) != 0u) return ArmUndefinedCallPlan();

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
        ctx.emit->Owner().Panic(
            "ArmPlanNeonShiftImmNarrowSat: imm6=000xxx word=0x%08X at guest pc=0x%08X", w,
            d.guest_address);
    }

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeonSat::HandleShiftImmNarrowSatHelper),
        {ArmCallPlanPointer(ctx.emit->NeonSat()), op, d_idx, m_idx, esize, shift_amount});
}

}
