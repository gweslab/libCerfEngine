#include <cerfengine/arm/arm_cpu.h>

#include "../arm_block_compiler.h"
#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrRaiseDataAbort(ArmIrContext& c) {
    ArmIrCallConst(c, reinterpret_cast<const void*>(&ArmBlockCompiler::RaiseAbortDataHelper),
                   cerfir::CallConv::kFastcall, 3u, c.d->guest_address,
                   cerfir::CallHostAddress(c.compiler), c.d->raw);
    c.b->ExitDispatcher();
}

void ArmIrRaisePreciseDataAbort(ArmIrContext& c) {
    ArmIrCallConst(c, reinterpret_cast<const void*>(&ArmCpu::RaiseAbortDataExceptionHelper),
                   cerfir::CallConv::kCdecl, 2u, cerfir::CallHostAddress(c.block->emit->Cpu()),
                   c.d->guest_address, 0u);
    c.b->ExitDispatcher();
}

}
