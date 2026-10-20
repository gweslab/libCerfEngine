#include <cerfengine/arm/arm_cpu.h>
#include "../arm_undefined_encoding_log.h"

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrRaiseUndefined(ArmIrContext& c) {
    ArmIrCallConst(c, reinterpret_cast<const void*>(&ArmCpu::RaiseUndefinedExceptionHelper),
                   cerfir::CallConv::kCdecl, 2u, cerfir::CallHostAddress(c.block->emit->Cpu()),
                   c.d->guest_address, 0u);
    c.b->ExitDispatcher();
}

void ArmIrUndefined(ArmIrContext& c) {
    LogUndefinedEncoding(c.d, c.block);
    ArmIrRaiseUndefined(c);
}

}
