#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_pair_min_max.h"
#include "plan_fns.h"

namespace cerfengine {

/* VPMAX / VPMIN (floating-point) - A8.8.366, opc=1111 C=0 U=1. */
ArmCallPlan ArmPlanNeonData3SameFpPairMinMax(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >>  7) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t sz    = (w >> 20) & 1u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.366: sz==1 || Q==1 UND. */
    if (sz != 0u || Q != 0u) return ArmUndefinedCallPlan();

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon3SameFpPairMinMax::Handle3SameFpPairMinMaxHelper),
        {ArmCallPlanPointer(ctx.emit->Neon3SameFpPairMinMax()), op, d_idx, n_idx, m_idx});
}

}
