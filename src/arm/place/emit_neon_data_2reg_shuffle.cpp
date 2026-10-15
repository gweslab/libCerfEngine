#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_shuffle.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VTRN / VUZP / VZIP (A8.8.420 / A8.8.422 / A8.8.423) - A7.4.5 A=10,
   bits[10:7]=0001/0010/0011. */
uint8_t* PlaceNeonData2RegShuffle(uint8_t*      cursor,
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

    /* size==11 UND per A8.8.420 / A8.8.422 / A8.8.423. */
    if (size == 3u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (op != ArmNeon2RegShuffle::kTrn && Q == 0u && size == 2u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t esize = 8u << size;

    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, Q);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegShuffle())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegShuffle::HandleShuffleHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
