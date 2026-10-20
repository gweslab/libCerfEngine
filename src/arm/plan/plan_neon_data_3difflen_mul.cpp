#include "../arm_emit_services.h"
#include "../arm_neon_3difflen.h"
#include "plan_fns.h"

namespace cerfengine {

/* 3-reg-different-length Multiply Long family
   (A8.8.336 VMLAL/VMLSL T2/A2, A8.8.350 VMULL T2/A2). D op D -> Q. */
ArmCallPlan ArmPlanNeonData3DiffLenMul(const DecodedInsn& d, const BlockContext& ctx) {
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

    if (size == 3u) {
        ctx.emit->Owner().Panic("ArmPlanNeonData3DiffLenMul: size=3 word=0x%08X at guest pc=0x%08X",
                                w, d.guest_address);
    }
    if ((d_idx & 1u) != 0u) return ArmUndefinedCallPlan();
    /* A8.8.350 (p. A8-958) T2/A2: "if op == '1' && (U != '0' || size != '00') then
       UNDEFINED". */
    if (op == ArmNeon3DiffLen::kDlMullPoly && (U != 0u || size != 0u)) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon3DiffLen::HandleMulLongHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon3DiffLen()), op, d_idx, n_idx, m_idx,
                           esize});
}

}
