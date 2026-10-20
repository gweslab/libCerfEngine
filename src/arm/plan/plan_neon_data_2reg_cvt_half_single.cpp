#include "../arm_emit_services.h"
#include "../arm_neon_2reg_cvt_half_single.h"
#include "plan_fns.h"

namespace cerfengine {

/* VCVT half↔single Advanced SIMD (A8.8.310) - A7.4.5 A=10, bits[10:7]=11x0
   with bit[6]=0. bit[8]=op selects direction (0=single→half, 1=half→single). */
ArmCallPlan ArmPlanNeonData2RegCvtHalfSingle(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* ARM DDI 0406C.c B4.1.109 (p. B4-1658): MVFR1 "A_SIMD HPFP, bits[23:20]
       Indicates whether the Advanced SIMD Extension implements half-precision
       floating-point conversion instructions. 0b0000 Not implemented." */
    if (((ctx.emit->ProcessorConfig()->mvfr1 >> 20) & 0xFu) == 0u) return ArmUndefinedCallPlan();
    /* ARM DDI 0406C.c A8.8.310 (p. A8-878): "if size != '01' then UNDEFINED;
       if half_to_single && Vd<0> == '1' then UNDEFINED; if !half_to_single &&
       Vm<0> == '1' then UNDEFINED". */
    if (size != 1u) return ArmUndefinedCallPlan();
    if (op == ArmNeon2RegCvtHalfSingle::kHalfToSingle && (d_idx & 1u)) return ArmUndefinedCallPlan();
    if (op == ArmNeon2RegCvtHalfSingle::kSingleToHalf && (m_idx & 1u)) return ArmUndefinedCallPlan();

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegCvtHalfSingle::HandleCvtHalfSingleHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegCvtHalfSingle()), op, d_idx, m_idx});
}

}
