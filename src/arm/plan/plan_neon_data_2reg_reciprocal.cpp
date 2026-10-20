#include "../arm_emit_services.h"
#include "../arm_neon_2reg_reciprocal.h"
#include "plan_fns.h"

namespace cerfengine {

/* VRECPE / VRSQRTE (A8.8.384 / A8.8.391) - A7.4.5 A=11, bits[10:9]=10,
   bit[8]=F integer/.F32, bit[7] selects op (0=VRECPE, 1=VRSQRTE). */
ArmCallPlan ArmPlanNeonData2RegReciprocal(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t F     = (w >>  8) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.384 / A8.8.391: size != 10 UND. */
    if (size != 2u) return ArmUndefinedCallPlan();
    /* A8.8.384 / A8.8.391. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegReciprocal::HandleReciprocalHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegReciprocal()), op, F, d_idx, m_idx, regs});
}

}
