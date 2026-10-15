#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_unimplemented_halt.h"
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceArmUnimplemented(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->UnimplementedHalt())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmUnimplementedHalt::InstructionHelper));
    return cursor;
}

}
