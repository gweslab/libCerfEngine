#include "../arm_emit_services.h"
#include "../arm_neon_2reg_shuffle.h"
#include "plan_fns.h"

namespace cerfengine {

/* VTRN / VUZP / VZIP (A8.8.420 / A8.8.422 / A8.8.423) - A7.4.5 A=10,
   bits[10:7]=0001/0010/0011. */
ArmCallPlan ArmPlanNeonData2RegShuffle(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t op    = d.op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* size==11 UND per A8.8.420 / A8.8.422 / A8.8.423. */
    if (size == 3u) return ArmUndefinedCallPlan();
    if (op != ArmNeon2RegShuffle::kTrn && Q == 0u && size == 2u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t esize = 8u << size;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegShuffle::HandleShuffleHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegShuffle()), op, esize, Q, d_idx,
                           m_idx});
}

}
