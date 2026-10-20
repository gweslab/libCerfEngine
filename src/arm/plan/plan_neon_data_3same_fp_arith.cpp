#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_arith.h"
#include "plan_fns.h"

namespace cerfengine {

/* VADD.F32 / VSUB.F32 / VMUL.F32 / VABD.F32 (A8.8.283 / A8.8.415 /
   A8.8.351 / A8.8.279) - A7.4.1 opc=1101. */
ArmCallPlan ArmPlanNeonData3SameFpArith(const DecodedInsn& d, const BlockContext& ctx) {
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

    /* A8.8.283 / A8.8.415 / A8.8.351 /
       A8.8.279: sz==1 UND. */
    if (sz != 0u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon3SameFpArith::Handle3SameFpArithHelper),
        {ArmCallPlanPointer(ctx.emit->Neon3SameFpArith()), op, d_idx, n_idx, m_idx, regs});
}

}
