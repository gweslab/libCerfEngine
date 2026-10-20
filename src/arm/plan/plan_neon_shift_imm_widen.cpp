#include "../arm_emit_services.h"
#include "../arm_neon_shift_imm.h"
#include "plan_fns.h"

namespace cerfengine {

/* Widening left shift family: VSHLL T1/A1 (A8.8.397). */
ArmCallPlan ArmPlanNeonShiftImmWiden(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t L_bit = (w >> 7)  & 1u;
    const uint32_t B_bit = (w >> 6)  & 1u;
    const uint32_t imm6  = (w >> 16) & 0x3Fu;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (L_bit != 0u || B_bit != 0u || (d_idx & 1u) != 0u) return ArmUndefinedCallPlan();

    /* ARM DDI 0406C.c A8.8.397 (p. A8-1050) imm6 case table. */
    uint32_t esize, shift_amount;
    if (imm6 & 0x20u) {
        esize        = 32u;
        shift_amount = imm6 - 32u;
    } else if (imm6 & 0x10u) {
        esize        = 16u;
        shift_amount = imm6 - 16u;
    } else if (imm6 & 0x08u) {
        esize        = 8u;
        shift_amount = imm6 - 8u;
    } else {
        /* ARM DDI 0406C.c A8.8.397 (p. A8-1050): "if imm6 IN "000xxx" then SEE
           "Related encodings"" - One register and a modified immediate value
           (p. A7-269). */
        ctx.emit->Owner().Panic(
            "ArmPlanNeonShiftImmWiden: imm6=000xxx word=0x%08X at guest pc=0x%08X", w,
            d.guest_address);
    }

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeonShiftImm::HandleShiftImmWidenHelper),
        {ArmCallPlanPointer(ctx.emit->NeonShiftImm()), op, d_idx, m_idx, esize, shift_amount});
}

}
