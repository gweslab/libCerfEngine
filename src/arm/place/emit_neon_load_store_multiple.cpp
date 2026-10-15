#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VLD1 / VST1 (multiple single elements), DDI0406C A8.8.320 / A8.8.404.
   type -> regs: 0111=1, 1010=2, 0110=3, 0010=4. */
uint8_t* PlaceNeonLoadStoreMultiple(uint8_t*      cursor,
                                    DecodedInsn*  d,
                                    BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t type  = d->op1;
    const uint32_t regs  = (type == 0x7u) ? 1u : (type == 0xAu) ? 2u
                         : (type == 0x6u) ? 3u : 4u;
    const uint32_t align = d->crm;

    /* Per-type alignment UNDEFINED constraints (A8.8.320/404). */
    if (((type == 0x7u || type == 0x6u) && (align & 2u)) ||
        (type == 0xAu && align == 3u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t d_idx      = (d->n << 4) | d->crn;
    const uint32_t align_log2 = align ? (align + 2u) : 0u;
    const uint32_t flags      = (d->l & 1u) | (regs << 1) | (align_log2 << 4)
                              | ((d->cp & 0x3u) << 7);

    EmitPush32(cursor, flags);
    EmitPush32(cursor, d->rm);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmNeon::HandleLoadStoreMultipleHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRet(cursor);
    FixupLabel(continue_label, cursor);
    return cursor;
}

}
