#include "../arm_emit_services.h"
#include "../arm_neon_2reg_unary_arith.h"
#include "plan_fns.h"

namespace cerfengine {

/* VABS / VNEG (A8.8.280 / A8.8.355) - A7.4.5 A=01, bits[9:7]=110/111.
   bit[10]=F selects integer (0) vs .F32 (1). bit[6]=Q selects D vs Q form. */
ArmCallPlan ArmPlanNeonData2RegUnaryArith(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t F     = (w >> 10) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    if (size == 3u) return ArmUndefinedCallPlan();
    if (F == 1u && size != 2u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegUnaryArith::HandleUnaryArithHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegUnaryArith()), op, F, esize, d_idx, m_idx, regs});
}

}
