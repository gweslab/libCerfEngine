#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include <cerfengine/engine.h>

namespace cerfengine {

uint8_t* PlaceMSRImmediate(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    using namespace x86;

    if (d->crn == 0u && ctx->emit->ProcessorConfig()->arch_v7) {
        if (d->n == 0u) {
            ctx->emit->Owner().Panic("PlaceMSRImmediate: hint encoding (mask==0, R==0) "
                                     "routed here at pc=0x%08X", d->guest_address);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* operand = 8_bit_immediate ROR (rotate_imm * 2) - ARM DDI 0100I
       A4.1.39 (p. A4-76); ARM DDI 0406C.c A8.8.111 (p. A8-498). */
    const uint32_t imm8  = d->immediate & 0xFFu;
    const uint32_t rot   = ((d->immediate >> 8) & 0xFu) * 2u;
    const uint32_t value =
        rot ? ((imm8 >> rot) | (imm8 << (32u - rot))) : imm8;

    EmitMovRegImm32(cursor, kEax, value);
    return EmitMsrWriteTail(cursor, d, ctx);
}

}
