#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

/* VDUP (ARM core register), DDI0406C A8.8.314, a cp11 register transfer
   (Table A7-22). */
ArmCallPlan ArmPlanNeonVdupCore(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t Q     =  d.cp_opc       & 1u;
    const uint32_t B     = (d.cp_opc >> 1) & 1u;
    const uint32_t E     =  d.cp           & 1u;
    const uint32_t d_bit = (d.cp     >> 2) & 1u;
    const uint32_t d_idx = (d_bit << 4) | d.crn;
    const uint32_t rt    =  d.rd;
    const uint32_t regs  = Q ? 2u : 1u;
    const uint32_t be    = (B << 1) | E;
    const uint32_t esize = (be == 0u) ? 32u : (be == 1u) ? 16u
                         : (be == 2u) ? 8u : 0u;
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon::HandleVdupHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon()), d.guest_address, d_idx, rt, esize,
                           regs},
                          true);
}

}
