#include "../arm_emit_services.h"
#include "../arm_neon_3difflen.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-different-length Saturating Doubling Multiply Long family
   (A8.8.371 VQDMLAL/VQDMLSL T1/A1, A8.8.373 VQDMULL T1/A1). D op D -> Q,
   signed only, saturating with sticky FPSCR.QC. */
ArmCallPlan ArmPlanNeonData3DiffLenMulSat(const DecodedInsn& d, const BlockContext& ctx) {
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
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* Table A7-10 (p. A7-264) allocates these with U = 0; A7.4.2 (p. A7-264): "Other
       encodings in this space are UNDEFINED." */
    if (U != 0u) return ArmUndefinedCallPlan();
    if (size == 3u) {
        ctx.emit->Owner().Panic(
            "ArmPlanNeonData3DiffLenMulSat: size=3 word=0x%08X at guest pc=0x%08X", w,
            d.guest_address);
    }
    /* A8.8.371 (p. A8-998), A8.8.373 (p. A8-1002) T1/A1: "if size == '00' || Vd<0> == '1'
       then UNDEFINED". */
    if (size == 0u || (d_idx & 1u) != 0u) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon3DiffLen::HandleMulLongSatHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon3DiffLen()), op, d_idx, n_idx, m_idx,
                           esize});
}

}
