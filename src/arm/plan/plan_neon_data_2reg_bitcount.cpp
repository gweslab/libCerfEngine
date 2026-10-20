#include "../arm_emit_services.h"
#include "../arm_neon_2reg_bitcount.h"
#include "plan_fns.h"

namespace cerfengine {

/* VCLS / VCLZ / VCNT (A8.8.299 / A8.8.302 / A8.8.304). */
ArmCallPlan ArmPlanNeonData2RegBitcount(const DecodedInsn& d, const BlockContext& ctx) {
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

    /* VCLS/VCLZ: size==11 UND (A8.8.299, A8.8.302).
       VCNT: size!=00 UND (A8.8.304). */
    if (size == 3u) return ArmUndefinedCallPlan();
    if (op == ArmNeon2RegBitcount::kCnt && size != 0u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegBitcount::HandleBitcountHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegBitcount()), op, d_idx, m_idx,
                           esize, regs});
}

}
