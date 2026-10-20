#include <cerfengine/arm/arm_cpu.h>

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.228 SVC (p. A8-721): CallSupervisor(imm32<15:0>). */
void ArmIrSvc(ArmIrContext& c) {
    ArmIrCallConst(c, reinterpret_cast<const void*>(&ArmCpu::RaiseSwiExceptionHelper),
                   cerfir::CallConv::kCdecl, 2u, cerfir::CallHostAddress(c.block->emit->Cpu()),
                   c.d->guest_address, 0u);
    c.b->ExitDispatcher();
}

}
