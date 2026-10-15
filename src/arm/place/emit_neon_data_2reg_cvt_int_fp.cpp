#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_2reg_cvt_int_fp.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* VCVT int↔fp Advanced SIMD (A8.8.305) - A7.4.5 A=11, bits[10:9]=11,
   bits[8:7]=op. Also handles immediate-scaled VCVT (A8.8.307). */
uint8_t* PlaceNeonData2RegCvtIntFp(uint8_t*      cursor,
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
    const bool fixed = ((w >> 8) & 0xEu) == 0xEu;
    const uint32_t imm6 = (w >> 16) & 63u;
    const uint32_t frac_bits = fixed ? 64u - imm6 : 0u;

    if (fixed && (w & 0x80u)) {
        emit->Owner().Panic("PlaceNeonData2RegCvtIntFp: L=1 word=0x%08X at guest pc=0x%08X",
                            w, d->guest_address);
    }
    /* A8.8.305 (p. A8-868): "if size != '10' then UNDEFINED"; A8.8.307 (p. A8-872):
       "if imm6 IN '0xxxxx' then UNDEFINED". */
    if ((!fixed && size != 2u) || (fixed && (imm6 & 0x20u) == 0u)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    /* A8.8.305, A8.8.307. */
    if (Q != 0u && ((d_idx & 1u) || (m_idx & 1u))) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t regs = Q ? 2u : 1u;

    EmitPush32(cursor, frac_bits);
    EmitPush32(cursor, regs);
    EmitPush32(cursor, m_idx);
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Neon2RegCvtIntFp())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeon2RegCvtIntFp::HandleCvtIntFpHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
