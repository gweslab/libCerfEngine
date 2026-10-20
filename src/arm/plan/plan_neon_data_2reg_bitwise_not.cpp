#include "../arm_emit_services.h"
#include "../arm_neon_2reg_bitwise_not.h"
#include "plan_fns.h"

namespace cerfengine {

/* VMVN (register) - A8.8.354. */
ArmCallPlan ArmPlanNeonData2RegBitwiseNot(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t w     = d.immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* size != 00 UND per A8.8.354. */
    if (size != 0u) return ArmUndefinedCallPlan();
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) return ArmUndefinedCallPlan();

    const uint32_t regs = Q ? 2u : 1u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon2RegBitwiseNot::HandleMvnHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon2RegBitwiseNot()), d_idx, m_idx, regs});
}

}
