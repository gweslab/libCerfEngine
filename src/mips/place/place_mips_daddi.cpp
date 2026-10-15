#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsDaddi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t imm_lo = static_cast<uint32_t>(static_cast<int32_t>(
                                static_cast<int16_t>(d->imm)));
    const uint32_t imm_hi = (d->imm & 0x8000u) ? 0xFFFFFFFFu : 0x00000000u;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    Emit8(cursor, 0x05);                       /* ADD eax, imm32 - 05 id, sets CF
                                                  (SDM Vol. 2A 3-32 ADD) */
    Emit32(cursor, imm_lo);
    EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprHiOff(d->rs));
    Emit8(cursor, 0x81);                       /* ADC edx, imm32 - 81 /2 id
                                                  (SDM Vol. 2A 3-27 ADC) */
    EmitModRmReg(cursor, 3, kEdx, 2);
    Emit32(cursor, imm_hi);
    mips_emit::EmitTrappingArith64Tail(cursor, d->rt, ctx->emit->Exceptions(),
        reinterpret_cast<void*>(&MipsExceptionDelivery::ArithOverflowHelper),
        d->guest_address);
    return cursor;
}

}
