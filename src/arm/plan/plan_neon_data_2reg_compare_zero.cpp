#include "../arm_emit_services.h"
#include "../arm_neon_2reg_compare_zero.h"
#include "plan_fns.h"

namespace cerfengine {

/* VCEQ / VCGT / VCGE / VCLE / VCLT immediate #0 - A7.4.5 A=01, F-bit
   discriminates integer (F=0) vs .F32 (F=1). */
ArmCallPlan ArmPlanNeonData2RegCompareZero(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t F     = (w >> 10) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.292 / A8.8.294 / A8.8.296 /
       A8.8.298 / A8.8.301 - common UND form. */
    if (size == 3u) return ArmUndefinedCallPlan();
    if (F == 1u && size != 2u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegCompareZero::HandleCompareZeroHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegCompareZero()), op, F, esize, d_idx, m_idx, regs});
}

}
