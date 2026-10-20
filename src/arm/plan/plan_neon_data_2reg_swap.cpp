#include "../arm_emit_services.h"
#include "../arm_neon_2reg_swap.h"
#include "plan_fns.h"

namespace cerfengine {

/* VSWP (A8.8.418) - A7.4.5 A=10, bits[10:7]=0000. */
ArmCallPlan ArmPlanNeonData2RegSwap(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.418 (p. A8-1092): "if size != '00' then UNDEFINED". */
    if (size != 0u) return ArmUndefinedCallPlan();
    /* A8.8.418. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegSwap::HandleSwapHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegSwap()), d_idx, m_idx, regs});
}

}
