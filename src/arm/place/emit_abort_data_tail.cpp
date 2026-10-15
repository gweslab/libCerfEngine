#include <cstdint>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* EmitAbortDataTail(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->guest_address);
    EmitJmp32(cursor, ctx->raise_abort_data_helper_target);
    return cursor;
}

}
