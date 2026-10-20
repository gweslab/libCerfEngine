#include "../arm_emit_services.h"
#include "../arm_neon_2reg_sat_abs_neg.h"
#include "plan_fns.h"

namespace cerfengine {

/* VQABS / VQNEG (A8.8.369 / A8.8.375) - A7.4.5 A=00, bits[10:7]=1110/1111. */
ArmCallPlan ArmPlanNeonData2RegSatAbsNeg(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.369 / A8.8.375. */
    if (size == 3u) return ArmUndefinedCallPlan();
    /* A8.8.369 / A8.8.375. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegSatAbsNeg::HandleSatAbsNegHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegSatAbsNeg()), op, esize, d_idx, m_idx, regs});
}

}
