#include "../arm_emit_services.h"
#include "../arm_neon_shift_imm.h"
#include "plan_fns.h"

namespace cerfengine {

/* VSHLL T2/A2 (A8.8.397). */
ArmCallPlan ArmPlanNeonData2RegWiden(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.397: size==11 || Vd[0]==1 UND. */
    if (size == 3u || (d_idx & 1u)) return ArmUndefinedCallPlan();

    const uint32_t esize        = 8u << size;
    const uint32_t shift_amount = esize;
    const uint32_t op_sel       = ArmNeonShiftImm::kSiShllS;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeonShiftImm::HandleShiftImmWidenHelper),
        {ArmCallPlanPointer(ctx.emit->NeonShiftImm()), op_sel, d_idx, m_idx, esize, shift_amount});
}

}
