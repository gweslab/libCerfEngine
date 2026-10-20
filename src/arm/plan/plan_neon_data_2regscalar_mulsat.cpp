#include "../arm_emit_services.h"
#include "../arm_neon_2regscalar.h"
#include "plan_fns.h"

namespace cerfengine {

/* 2-reg-and-scalar Saturating Doubling Multiply Long form
   (A8.8.371 VQDMLAL/VQDMLSL T2/A2, A8.8.373 VQDMULL T2/A2). */
ArmCallPlan ArmPlanNeonData2RegScalarMulSat(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >> 7)  & 1u;
    const uint32_t Mbit  = (w >> 5)  & 1u;
    const uint32_t U     = (w >> 24) & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;

    /* Table A7-11 (p. A7-265) allocates these with U = 0; A7.4.3 (p. A7-265): "Other
       encodings in this space are UNDEFINED." */
    if (U != 0u) return ArmUndefinedCallPlan();
    if (size == 3u) {
        ctx.emit->Owner().Panic(
            "ArmPlanNeonData2RegScalarMulSat: size=3 word=0x%08X at guest pc=0x%08X", w,
            d.guest_address);
    }
    /* A8.8.371 (p. A8-998), A8.8.373 (p. A8-1002) T2/A2: "if size == '00' || Vd<0> == '1'
       then UNDEFINED". */
    if (size == 0u || (d_idx & 1u) != 0u) return ArmUndefinedCallPlan();

    /* Scalar extraction per A8.8.371. */
    uint32_t esize, m_idx, index;
    if (size == 1u) {
        esize = 16u;
        m_idx = Vm & 0x7u;
        index = (Mbit << 1) | ((Vm >> 3) & 1u);
    } else {
        esize = 32u;
        m_idx = Vm;
        index = Mbit;
    }

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon2RegScalar::HandleScalarMulLongSatHelper),
        {ArmCallPlanPointer(ctx.emit->Neon2RegScalar()), op, d_idx, n_idx, m_idx, esize, index});
}

}
