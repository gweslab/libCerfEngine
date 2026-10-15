#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsDadd(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitTrappingArith64RR(cursor, d->rd, d->rs, d->rt,
        0x03, 0x13, ctx->emit->Exceptions(),
        reinterpret_cast<void*>(&MipsExceptionDelivery::ArithOverflowHelper),
        d->guest_address);
    return cursor;
}

}
