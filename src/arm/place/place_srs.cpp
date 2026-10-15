#include <cstddef>

#include "../arm_emit_services.h"
#include "../arm_exception_frame.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceSrs(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    const uint32_t encoded =
        (static_cast<uint32_t>(d->p)         << 7) |
        (static_cast<uint32_t>(d->u)         << 6) |
        (static_cast<uint32_t>(d->w)         << 5) |
        (static_cast<uint32_t>(d->immediate) & 0x1Fu);
    EmitMovRegImm32(cursor, kEcx, encoded);

    EmitMovRegImm32(cursor, kEdx,
        static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->ExceptionFrame())));

    EmitPush32(cursor, d->guest_address);

    EmitCall(cursor, reinterpret_cast<void*>(&ArmExceptionFrame::SrsHelper));

    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* stored = EmitJzLabel32(cursor);
    cursor = EmitAbortDataTail(cursor, d, ctx);
    FixupLabel32(stored, cursor);
    return cursor;
}

}
