#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsAddi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));

    if (sext == 0) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
        if (d->rt != 0) {
            mips_emit::EmitStoreGprSextEax(cursor, d->rt);
        }
        return cursor;
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEax, sext);
    mips_emit::EmitTrappingArith32Tail(cursor, d->rt, ctx->emit->Exceptions(),
        reinterpret_cast<void*>(&MipsExceptionDelivery::ArithOverflowHelper),
        d->guest_address);
    return cursor;
}

}
