#include "../arm_block_compiler.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrRaiseDataAbort(ArmIrContext& c) {
    ArmIrCallConst(c, reinterpret_cast<const void*>(&ArmBlockCompiler::RaiseAbortDataHelper),
                   cerfir::CallConv::kFastcall, 3u, c.d->guest_address,
                   cerfir::CallHostAddress(c.compiler), c.d->raw);
    c.b->ExitDispatcher();
}

}
