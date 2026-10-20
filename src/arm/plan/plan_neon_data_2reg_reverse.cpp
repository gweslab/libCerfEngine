#include "../arm_emit_services.h"
#include "../arm_neon_2reg_reverse.h"
#include "plan_fns.h"

namespace cerfengine {

/* VREV16/VREV32/VREV64 (A8.8.386). bit[6]=Q selects D vs Q form. */
ArmCallPlan ArmPlanNeonData2RegReverse(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t opbit = (w >>  7) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* op + size >= 3 UND per A8.8.386. */
    if (opbit + size >= 3u) return ArmUndefinedCallPlan();
    /* Q && (Vd<0> || Vm<0>) UND per A8.8.386. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize     = 8u << size;
    const uint32_t groupsize = 1u << (3u - opbit - size);
    const uint32_t regs      = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegReverse::HandleRevHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegReverse()), op, d_idx, m_idx, esize,
                           groupsize, regs});
}

}
