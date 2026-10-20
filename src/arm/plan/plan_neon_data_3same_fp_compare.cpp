#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_compare.h"
#include "plan_fns.h"

namespace cerfengine {

/* VCEQ.F32 / VCGE.F32 / VCGT.F32 (register) - A8.8.291 / A8.8.293 / A8.8.295,
   opc=1110 C=0. (u, bit[21]) selects EQ / GE / GT. */
ArmCallPlan ArmPlanNeonData3SameFpCompare(const DecodedInsn& d, const BlockContext& ctx) {
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

    /* A8.8.291 (and siblings): sz==1 UND. */
    if (sz != 0u) return ArmUndefinedCallPlan();
    /* A8.8.291: Q==1 with odd reg index UND. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon3SameFpCompare::Handle3SameFpCompareHelper),
        {ArmCallPlanPointer(ctx.emit->Neon3SameFpCompare()), op, d_idx, n_idx, m_idx, regs});
}

}
