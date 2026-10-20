#include "../arm_emit_services.h"
#include "../arm_neon_2reg_scalar_mul.h"
#include "plan_fns.h"

namespace cerfengine {

/* VMLA / VMLS / VMUL by scalar T1/A1 (A8.8.338 / A8.8.352).
   bit[24]=Q D/Q form. bit[8]=F integer/.F32. */
ArmCallPlan ArmPlanNeonData2RegScalarMul(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >>  7) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >> 24) & 1u;
    const uint32_t F     = (w >>  8) & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;

    if (size == 3u) {
        ctx.emit->Owner().Panic("ArmPlanNeonData2RegScalarMul: size=3 word=0x%08X at guest pc=0x%08X",
                                w, d.guest_address);
    }
    /* A8.8.338 (p. A8-934), A8.8.352 (p. A8-962): "if size == '00' || (F == '1' &&
       size == '01') then UNDEFINED". */
    if (size == 0u || (F == 1u && size == 1u)) return ArmUndefinedCallPlan();
    /* A8.8.338 / A8.8.352: Q && (Vd[0] || Vn[0]) UND. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u))) return ArmUndefinedCallPlan();

    uint32_t esize, m_idx, index;
    if (size == 1u) {
        /* A8.8.338 / A8.8.352. */
        esize = 16u;
        m_idx = Vm & 0x7u;
        index = (Mbit << 1) | ((Vm >> 3) & 1u);
    } else {
        /* size == 2 (A8.8.338 / A8.8.352). */
        esize = 32u;
        m_idx = Vm;
        index = Mbit;
    }

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegScalarMul::HandleScalarMulHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegScalarMul()), op, F, esize, d_idx, n_idx, m_idx,
         index, regs});
}

}
