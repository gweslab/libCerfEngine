#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_scalar_mul.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VMLA / VMLS / VMUL by scalar T1/A1 (A8.8.338 / A8.8.352).
   bit[24]=Q D/Q form. bit[8]=F integer/.F32. */
uint8_t* PlaceNeonData2RegScalarMul(uint8_t*      cursor,
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
    const uint32_t Q     = (w >> 24) & 1u;
    const uint32_t F     = (w >>  8) & 1u;
    const uint32_t size  = (w >> 20) & 3u;
    const uint32_t d_idx = (Dbit << 4) | Vd;
    const uint32_t n_idx = (Nbit << 4) | Vn;

    if (size == 3u) {
        emit->Owner().Panic("PlaceNeonData2RegScalarMul: size=3 word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }
    /* A8.8.338 (p. A8-934), A8.8.352 (p. A8-962): "if size == '00' || (F == '1' &&
       size == '01') then UNDEFINED". */
    if (size == 0u || (F == 1u && size == 1u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    /* A8.8.338 / A8.8.352: Q && (Vd[0] || Vn[0]) UND. */
    if (Q != 0u && ((d_idx & 1u) || (n_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint32_t esize, m_idx, index;
    if (size == 1u) {
        /* A8.8.338 / A8.8.352. */
        esize = 16u;
        m_idx = Vm & 0x7u;
        index = (Mbit << 1) | ((Vm >> 3) & 1u);
    } else {
        /* size == 2 (A8.8.338 / A8.8.352). */
        esize = 32u;
        m_idx = Vm;
        index = Mbit;
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, index);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, n_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, esize);
    EmitPush32(cursor, F);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegScalarMul())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegScalarMul::HandleScalarMulHelper));
    EmitAddRegImm32(cursor, kEsp, 36);
    return cursor;
}

}
