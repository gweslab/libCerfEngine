#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

ArmCallPlan ArmPlanNeonLoadAllLanes(const DecodedInsn& d, const BlockContext& ctx) {
    return ArmPlannedCall(reinterpret_cast<const void*>(&ArmNeon::LoadAllLanesHelper),
                          {ArmCallPlanPointer(ctx.emit->Neon()), d.guest_address, d.immediate},
                          true);
}

ArmCallPlan ArmPlanNeonLoadStoreSingleLane(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t size = d.cp;
    const uint32_t N    = d.op1 + 1u;
    const uint32_t ia   = d.crm;

    uint32_t index, inc = 1u;
    if (size == 0u) {
        index = ia >> 1;
    } else if (size == 1u) {
        index = ia >> 2;          inc = (ia & 2u) ? 2u : 1u;
    } else {
        index = ia >> 3;          inc = (ia & 4u) ? 2u : 1u;
    }

    /* Per-(N,size) UNDEFINED + alignment (A8.8.321/324/327/330). */
    const uint32_t ia0  = ia & 1u;
    const uint32_t ia10 = ia & 3u;
    bool     undef     = false;
    uint32_t alignment = 1u;
    switch (N) {
        case 1u:
            if (size == 0u)      { undef = (ia0 != 0u); }
            else if (size == 1u) { undef = ((ia & 2u) != 0u); alignment = ia0 ? 2u : 1u; }
            else                 { undef = ((ia & 4u) != 0u) || (ia10 != 0u && ia10 != 3u);
                                   alignment = (ia10 == 0u) ? 1u : 4u; }
            break;
        case 2u:
            if (size == 0u)      { alignment = ia0 ? 2u : 1u; }
            else if (size == 1u) { alignment = ia0 ? 4u : 1u; }
            else                 { undef = ((ia & 2u) != 0u); alignment = ia0 ? 8u : 1u; }
            break;
        case 3u:
            if (size == 2u)      { undef = (ia10 != 0u); }
            else                 { undef = (ia0 != 0u); }
            break;
        default:
            if (size == 0u)      { alignment = ia0 ? 4u : 1u; }
            else if (size == 1u) { alignment = ia0 ? 8u : 1u; }
            else                 { undef = (ia10 == 3u);
                                   alignment = (ia10 == 0u) ? 1u : (4u << ia10); }
            break;
    }
    if (undef) return ArmUndefinedCallPlan();

    const uint32_t d_idx = (d.n << 4) | d.crn;
    uint32_t align_log2 = 0u;
    while ((1u << align_log2) < alignment) ++align_log2;
    const uint32_t flags = (d.l & 1u)
                         | ((N - 1u)     << 1)
                         | ((inc - 1u)   << 3)
                         | (size         << 4)
                         | (index        << 6)
                         | (align_log2   << 9);

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon::HandleLoadStoreSingleLaneHelper),
        {ArmCallPlanPointer(ctx.emit->Neon()), d.guest_address, d_idx, d.rn, d.rm, flags}, true);
}

}
