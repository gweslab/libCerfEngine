#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_mul_acc.h"
#include "plan_fns.h"

namespace cerfengine {

/* VMLA.F32 / VMLS.F32 (A8.8.337) - A7.4.1 opc=1101, B=1, U=0. */
ArmCallPlan ArmPlanNeonData3SameFpMulAcc(const DecodedInsn& d, const BlockContext& ctx) {
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

    /* A8.8.337: sz==1 UND. */
    if (sz != 0u) return ArmUndefinedCallPlan();
    /* A8.8.337. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon3SameFpMulAcc::Handle3SameFpMulAccHelper),
        {ArmCallPlanPointer(ctx.emit->Neon3SameFpMulAcc()), op, d_idx, n_idx, m_idx, regs});
}

}
