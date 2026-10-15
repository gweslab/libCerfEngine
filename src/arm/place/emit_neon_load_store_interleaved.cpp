#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VLD2/3/4 + VST2/3/4 (multiple N-element structures, de-interleaved),
   DDI0406C A8.8.323/326/329 (load), A8.8.406/408/410 (store). */
uint8_t* PlaceNeonLoadStoreInterleaved(uint8_t*      cursor,
                                       DecodedInsn*  d,
                                       BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t type  = d->op1;
    const uint32_t size  = d->cp;
    const uint32_t align = d->crm;

    uint32_t nstreams, regs, inc;
    switch (type) {
        case 0x8u: nstreams = 2u; regs = 1u; inc = 1u; break;
        case 0x9u: nstreams = 2u; regs = 1u; inc = 2u; break;
        case 0x3u: nstreams = 2u; regs = 2u; inc = 2u; break;
        case 0x4u: nstreams = 3u; regs = 1u; inc = 1u; break;
        case 0x5u: nstreams = 3u; regs = 1u; inc = 2u; break;
        case 0x0u: nstreams = 4u; regs = 1u; inc = 1u; break;
        default:   nstreams = 4u; regs = 1u; inc = 2u; break;
    }

    /* UNDEFINED (A8.8.323/326/329): size==11 (no 64-bit); VLD3
       align<1>==1; VLD2 single-group (type 8/9) align==11. */
    if (size == 3u ||
        (nstreams == 3u && (align & 2u)) ||
        ((type == 0x8u || type == 0x9u) && align == 3u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t d_idx = (d->n << 4) | d->crn;
    const uint32_t align_log2 = (nstreams == 3u)
        ? ((align & 1u) ? 3u : 0u)
        : (align ? (align + 2u) : 0u);
    const uint32_t flags = (d->l & 1u)
                         | ((nstreams - 2u) << 1)
                         | ((regs - 1u)     << 3)
                         | ((inc - 1u)      << 4)
                         | (size            << 5)
                         | (align_log2      << 7);

    EmitPush32(cursor, flags);
    EmitPush32(cursor, d->rm);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmNeon::HandleLoadStoreInterleavedHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRet(cursor);
    FixupLabel(continue_label, cursor);
    return cursor;
}

}
