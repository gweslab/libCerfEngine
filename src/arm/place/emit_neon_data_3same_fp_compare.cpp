#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_3same_fp_compare.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VCEQ.F32 / VCGE.F32 / VCGT.F32 (register) - A8.8.291 / A8.8.293 / A8.8.295,
   opc=1110 C=0. (u, bit[21]) selects EQ / GE / GT. */
uint8_t* PlaceNeonData3SameFpCompare(uint8_t*      cursor,
                                     DecodedInsn*  d,
                                     BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t op    = d->op1;
    const uint32_t Vn    = (w >> 16) & 0xFu;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Vm    =  w        & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Nbit  = (w >>  7) & 1u;
    const uint32_t Mbit  = (w >>  5) & 1u;
    const uint32_t Q     = (w >>  6) & 1u;
    const uint32_t sz    = (w >> 20) & 1u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;
    const uint32_t m_idx = (Mbit << 4) | Vm;

    /* A8.8.291 (and siblings): sz==1 UND. */
    if (sz != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    /* A8.8.291: Q==1 with odd reg index UND. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, n_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon3SameFpCompare())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon3SameFpCompare::Handle3SameFpCompareHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
