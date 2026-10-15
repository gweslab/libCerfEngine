#include <cstddef>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARMv7 WFI hint, encoding A1 (ARM ARM DDI 0406C.c §A8.8.425, page A8-1106). */
uint8_t* PlaceWfi(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
        static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->InterruptChannel())));
    EmitMovRegImm32(cursor, kEdx, d->guest_address + d->length);
    EmitCall(cursor, reinterpret_cast<void*>(&ArmInterruptChannel::WfiHelper));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* proceed = EmitJzLabel32(cursor);
    EmitRet(cursor);
    FixupLabel32(proceed, cursor);
    return cursor;
}

}
