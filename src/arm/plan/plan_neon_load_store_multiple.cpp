#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

/* VLD1 / VST1 (multiple single elements), DDI0406C A8.8.320 / A8.8.404.
   type -> regs: 0111=1, 1010=2, 0110=3, 0010=4. */
ArmCallPlan ArmPlanNeonLoadStoreMultiple(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t type  = d.op1;
    const uint32_t regs  = (type == 0x7u) ? 1u : (type == 0xAu) ? 2u
                         : (type == 0x6u) ? 3u : 4u;
    const uint32_t align = d.crm;

    /* Per-type alignment UNDEFINED constraints (A8.8.320/404). */
    if (((type == 0x7u || type == 0x6u) && (align & 2u)) ||
        (type == 0xAu && align == 3u)) {
        return ArmUndefinedCallPlan();
    }

    const uint32_t d_idx      = (d.n << 4) | d.crn;
    const uint32_t align_log2 = align ? (align + 2u) : 0u;
    const uint32_t flags      = (d.l & 1u) | (regs << 1) | (align_log2 << 4)
                              | ((d.cp & 0x3u) << 7);

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon::HandleLoadStoreMultipleHelper),
        {ArmCallPlanPointer(ctx.emit->Neon()), d.guest_address, d_idx, d.rn, d.rm, flags}, true);
}

}
