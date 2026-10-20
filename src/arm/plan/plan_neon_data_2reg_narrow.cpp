#include "../arm_emit_services.h"
#include "../arm_neon_2reg_narrow.h"
#include "plan_fns.h"

namespace cerfengine {

/* VMOVN / VQMOVUN / VQMOVN.S / VQMOVN.U (A8.8.347 / A8.8.374) - A7.4.5
   A=10, bits[10:8]=010 with op at bits[7:6] selecting the variant. */
ArmCallPlan ArmPlanNeonData2RegNarrow(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.347 / A8.8.374: size==11 UND. */
    if (size == 3u) return ArmUndefinedCallPlan();
    if (m_idx & 1u) return ArmUndefinedCallPlan();

    const uint32_t esize_out = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegNarrow::HandleNarrowHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegNarrow()), op, esize_out, d_idx,
                           m_idx});
}

}
