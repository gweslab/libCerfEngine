#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_swap.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VSWP (A8.8.418) - A7.4.5 A=10, bits[10:7]=0000. */
uint8_t* PlaceNeonData2RegSwap(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.418 (p. A8-1092): "if size != '00' then UNDEFINED". */
    if (size != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    /* A8.8.418. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegSwap())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegSwap::HandleSwapHelper));
    EmitAddRegImm32(cursor, kEsp, 16);
    return cursor;
}

}
