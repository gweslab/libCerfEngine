#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_vtbl.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VTBL / VTBX - A8.8.419, U=1, bit23=1, bits[21:20]=11, bits[11:10]=10,
   bit[4]=0. */
uint8_t* PlaceNeonDataVtbl(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t len   = (w >>  8) & 0x3u;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >>  7) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t op    = (w >>  6) & 1u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;
    const uint32_t length = len + 1u;

    /* A8.8.419: n+length > 32 is UNPREDICTABLE. */
    if (n_idx + length > 32u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t op_sel = op ? ArmNeonVtbl::kTbx : ArmNeonVtbl::kTbl;

    EmitPush32(cursor, length);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, n_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op_sel);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->NeonVtbl())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeonVtbl::HandleVtblHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
