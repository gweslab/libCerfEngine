#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-same accumulate ops: d = f(d_old, n*m). Encoding (VMLA/VMLS,
   A8.8.336): opc=1001, C=0, bit24=op (MLA=0/MLS=1). */
ArmCallPlan ArmPlanNeonData3SameAcc(const DecodedInsn& d, const BlockContext& ctx) {
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
    /* A8.8.336 (p. A8-930), A8.8.277 (p. A8-818) T1/A1: "if size == '11' then
       UNDEFINED". */
    if (size == 3u) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon::HandleSimd3SameAccHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon()), op, d_idx, n_idx, m_idx, esize,
                           regs});
}

}
