#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-same pairwise (A8.8.362 VPADD, A8.8.365 VPMAX/VPMIN). Q==1 is
   UNDEFINED (doubleword only); size==11 is UNDEFINED. */
ArmCallPlan ArmPlanNeonData3SamePairwise(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >> 7)  & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t Q     = (w >> 6)  & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (Q != 0u || size == 3u) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon::HandleSimd3SamePairwiseHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon()), op, d_idx, n_idx, m_idx, esize});
}

}
