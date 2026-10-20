#include "../arm_emit_services.h"
#include "../arm_neon_sat.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-same saturating ops (A8.8.370 VQADD, A8.8.382 VQSUB). All sizes
   valid (incl. I64). */
ArmCallPlan ArmPlanNeonData3SameSat(const DecodedInsn& d, const BlockContext& ctx) {
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

    if (Q && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeonSat::HandleSimd3SameSatHelper),
                          {ArmCallPlanPointer(ctx.emit->NeonSat()), op, d_idx, n_idx, m_idx, esize,
                           regs});
}

}
