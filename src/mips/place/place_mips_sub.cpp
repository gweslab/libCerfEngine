#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsSub(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitSubRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    mips_emit::EmitTrappingArith32Tail(cursor, d->rd, ctx->emit->Exceptions(),
        reinterpret_cast<void*>(&MipsExceptionDelivery::ArithOverflowHelper),
        d->guest_address);
    return cursor;
}

}
