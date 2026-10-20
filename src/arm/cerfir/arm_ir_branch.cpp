#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.18 B (p. A8-335) and A8.8.25 BL (p. A8-349); A2.5.2
   (p. A2-52): ITSTATE advances when the instruction completes normally. */
void ArmIrBranch(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    if (d.l != 0u) {
        ArmIrWriteGprConst(c, ArmGpr::kR14,
                           c.block->thumb ? ((d.guest_address + d.length) | 1u)
                                          : (d.guest_address + 4u));
    }
    const uint32_t target = ArmPcReadValue(&d, c.block) + static_cast<uint32_t>(d.offset);
    if (d.itstate_after_valid != 0u && d.is_exception_return == 0u) {
        ArmIrStoreItState(c, d.itstate_after);
    }
    b.ExitDirect(target, 0u, true, true);
}

}
