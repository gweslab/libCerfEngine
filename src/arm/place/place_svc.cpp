#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* SVC A8.8.228 (DDI 0406C.c pp. A8-720/721): Operation is
   CallSupervisor(imm32<15:0>) - the Supervisor Call exception,
   p. B1-1210. */
uint8_t* PlaceSvc(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::RaiseSwiExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitRet(cursor);
    return cursor;
}

}
