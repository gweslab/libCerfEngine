#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_fma.h"
#include "plan_fns.h"

namespace cerfengine {

/* VFMA / VFMS - A8.8.317 Advanced SIMD T1/A1, opc=1100 C=1 U=0. */
ArmCallPlan ArmPlanNeonData3SameFpFma(const DecodedInsn& d, const BlockContext& ctx) {
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

    /* ARM DDI 0406C.c A8.8.317 (p. A8-892): "Encoding T1/A1  Advanced SIMDv2
       (UNDEFINED in integer-only variant)". B4.1.109 (p. B4-1658): MVFR1
       "A_SIMD FMAC, bits[31:28] ... 0b0000 Not implemented." */
    if (((ctx.emit->ProcessorConfig()->mvfr1 >> 28) & 0xFu) == 0u) return ArmUndefinedCallPlan();
    /* A8.8.317 (p. A8-892): "if sz == '1' then UNDEFINED". */
    if (sz != 0u) return ArmUndefinedCallPlan();
    /* A8.8.317 (p. A8-892): "if Q == '1' && (Vd<0> == '1' || Vn<0> == '1' ||
       Vm<0> == '1') then UNDEFINED". */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon3SameFpFma::Handle3SameFpFmaHelper),
        {ArmCallPlanPointer(ctx.emit->Neon3SameFpFma()), op, d_idx, n_idx, m_idx, regs});
}

}
