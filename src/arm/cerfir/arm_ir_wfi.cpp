#include <cerfengine/arm/arm_interrupt_channel.h>

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrWfi(ArmIrContext& c) {
    const DecodedInsn&     d = *c.d;
    cerfir::Builder&       b = *c.b;
    const cerfir::CallDesc wfi{reinterpret_cast<const void*>(&ArmInterruptChannel::WfiHelper),
                               cerfir::CallConv::kFastcall, 2u};
    const cerfir::Ref stop    = b.Call(wfi, cerfir::Type::kI1,
                                       b.ConstPtr(c.block->emit->InterruptChannel()),
                                       b.Const32(d.guest_address + d.length), cerfir::kNoRef);
    const uint32_t    proceed = b.NewLabel();
    b.Branch(stop, proceed, false);
    b.ExitDispatcher();
    b.Label(proceed);
}

}
