#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_bitcount.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VCLS / VCLZ / VCNT (A8.8.299 / A8.8.302 / A8.8.304). */
uint8_t* PlaceNeonData2RegBitcount(uint8_t*      cursor,
                                   DecodedInsn*  d,
                                   BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t op    = d->op1;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t size  = (w >> 18) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* VCLS/VCLZ: size==11 UND (A8.8.299, A8.8.302).
       VCNT: size!=00 UND (A8.8.304). */
    if (size == 3u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (op == ArmNeon2RegBitcount::kCnt && size != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t esize = 8u << size;
    const uint32_t regs  = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegBitcount())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegBitcount::HandleBitcountHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
