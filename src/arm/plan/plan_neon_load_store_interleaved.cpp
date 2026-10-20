#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include "plan_fns.h"

namespace cerfengine {

/* VLD2/3/4 + VST2/3/4 (multiple N-element structures, de-interleaved),
   DDI0406C A8.8.323/326/329 (load), A8.8.406/408/410 (store). */
ArmCallPlan ArmPlanNeonLoadStoreInterleaved(const DecodedInsn& d, const BlockContext& ctx) {
    const uint32_t type  = d.op1;
    const uint32_t size  = d.cp;
    const uint32_t align = d.crm;

    uint32_t nstreams, regs, inc;
    switch (type) {
        case 0x8u: nstreams = 2u; regs = 1u; inc = 1u; break;
        case 0x9u: nstreams = 2u; regs = 1u; inc = 2u; break;
        case 0x3u: nstreams = 2u; regs = 2u; inc = 2u; break;
        case 0x4u: nstreams = 3u; regs = 1u; inc = 1u; break;
        case 0x5u: nstreams = 3u; regs = 1u; inc = 2u; break;
        case 0x0u: nstreams = 4u; regs = 1u; inc = 1u; break;
        default:   nstreams = 4u; regs = 1u; inc = 2u; break;
    }

    /* UNDEFINED (A8.8.323/326/329): size==11 (no 64-bit); VLD3
       align<1>==1; VLD2 single-group (type 8/9) align==11. */
    if (size == 3u ||
        (nstreams == 3u && (align & 2u)) ||
        ((type == 0x8u || type == 0x9u) && align == 3u)) {
        return ArmUndefinedCallPlan();
    }

    const uint32_t d_idx = (d.n << 4) | d.crn;
    const uint32_t align_log2 = (nstreams == 3u)
        ? ((align & 1u) ? 3u : 0u)
        : (align ? (align + 2u) : 0u);
    const uint32_t flags = (d.l & 1u)
                         | ((nstreams - 2u) << 1)
                         | ((regs - 1u)     << 3)
                         | ((inc - 1u)      << 4)
                         | (size            << 5)
                         | (align_log2      << 7);

    return ArmPlannedCall(
        reinterpret_cast<const void*>(&ArmNeon::HandleLoadStoreInterleavedHelper),
        {ArmCallPlanPointer(ctx.emit->Neon()), d.guest_address, d_idx, d.rn, d.rm, flags}, true);
}

}
