#include "../arm_emit_services.h"
#include "../arm_neon_vext.h"
#include "plan_fns.h"

namespace cerfengine {

/* VEXT - A8.8.316, U=0, bit23=1, bits[21:20]=11, bit[4]=0. */
ArmCallPlan ArmPlanNeonDataVext(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t imm4  = (w >>  8) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >>  7) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.316: Q==1 with odd reg index UND. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();
    /* A8.8.316 (p. A8-890): "if Q == '0' && imm4<3> == '1' then UNDEFINED". */
    if (Q == 0u && ((imm4 >> 3) & 1u)) return ArmUndefinedCallPlan();

    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeonVext::HandleVextHelper),
                          {ArmCallPlanPointer(ctx.emit->NeonVext()), d_idx, n_idx, m_idx, imm4, Q});
}

}
