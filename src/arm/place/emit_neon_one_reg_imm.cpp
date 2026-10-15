#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_neon_one_reg_imm.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* AdvSIMDExpandImm (DDI 0406C.c p. A7-271, "Advanced SIMD expand immediate pseudocode"). */
uint64_t AdvSIMDExpandImm(uint32_t op, uint32_t cmode, uint32_t imm8) {
    const uint32_t cmode_31 = (cmode >> 1) & 0x7u;
    const uint32_t cmode_0  = cmode & 1u;
    const uint64_t v        = static_cast<uint64_t>(imm8);

    switch (cmode_31) {
        case 0x0u:
            return v | (v << 32);
        case 0x1u:
            return (v << 8) | (v << 40);
        case 0x2u:
            return (v << 16) | (v << 48);
        case 0x3u:
            return (v << 24) | (v << 56);
        case 0x4u:
            return v | (v << 16) | (v << 32) | (v << 48);
        case 0x5u:
            return (v << 8) | (v << 24) | (v << 40) | (v << 56);
        case 0x6u:
            if (cmode_0 == 0u) {
                const uint64_t lane32 = (v << 8) | 0xFFull;
                return lane32 | (lane32 << 32);
            } else {
                const uint64_t lane32 = (v << 16) | 0xFFFFull;
                return lane32 | (lane32 << 32);
            }
        case 0x7u:
        default:
            if (cmode_0 == 0u && op == 0u) {
                return v * 0x0101010101010101ull;
            } else if (cmode_0 == 0u && op == 1u) {
                uint64_t result = 0;
                for (uint32_t i = 0; i < 8u; ++i) {
                    if ((imm8 >> i) & 1u) {
                        result |= (0xFFull << (i * 8u));
                    }
                }
                return result;
            } else if (cmode_0 == 1u && op == 0u) {
                const uint32_t bit7    = (imm8 >> 7) & 1u;
                const uint32_t bit6    = (imm8 >> 6) & 1u;
                const uint32_t imm8_50 = imm8 & 0x3Fu;
                const uint32_t bit30   = bit6 ? 0u : 1u;
                const uint32_t bits29_25 = bit6 ? 0x1Fu : 0u;
                const uint32_t lane32 =
                    (bit7 << 31) | (bit30 << 30) | (bits29_25 << 25) | (imm8_50 << 19);
                return static_cast<uint64_t>(lane32) |
                       (static_cast<uint64_t>(lane32) << 32);
            }
            return 0ull;
    }
}

uint32_t DetermineOpType(uint32_t op_bit, uint32_t cmode) {
    const uint32_t cmode_0  = cmode & 1u;
    const uint32_t cmode_32 = (cmode >> 2) & 3u;
    if (op_bit == 1u) {
        if (cmode == 0xFu)  return UINT32_MAX;
        if (cmode == 0xEu)  return ArmNeonOneRegImm::kOpVmov;
        return cmode_0 ? ArmNeonOneRegImm::kOpVbic
                       : ArmNeonOneRegImm::kOpVmvn;
    }
    if (cmode_0 == 0u || cmode_32 == 3u) {
        return ArmNeonOneRegImm::kOpVmov;
    }
    return ArmNeonOneRegImm::kOpVorr;
}

}

uint8_t* PlaceNeonOneRegImm(uint8_t*      cursor,
                            DecodedInsn*  d,
                            BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t w     = d->immediate;
    const uint32_t Vd    = (w >> 12) & 0xFu;
    const uint32_t Dbit  = (w >> 22) & 1u;
    const uint32_t Q     = (w >> 6)  & 1u;
    const uint32_t op_b  = (w >> 5)  & 1u;
    const uint32_t cmode = (w >> 8)  & 0xFu;
    const uint32_t i_b   = (w >> 24) & 1u;
    const uint32_t imm3  = (w >> 16) & 0x7u;
    const uint32_t imm4  =  w        & 0xFu;
    const uint32_t imm8  = (i_b << 7) | (imm3 << 4) | imm4;
    const uint32_t d_idx = (Dbit << 4) | Vd;

    /* Q==1 requires Vd<0>==0 (A8.8.339). */
    if (Q != 0u && (d_idx & 1u) != 0u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t op_type = DetermineOpType(op_b, cmode);
    if (op_type == UINT32_MAX) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint64_t imm64 = AdvSIMDExpandImm(op_b, cmode, imm8);
    const uint32_t regs  = Q ? 2u : 1u;

    EmitPush32(cursor, regs);
    EmitPush32(cursor, static_cast<uint32_t>(imm64 >> 32));
    EmitPush32(cursor, static_cast<uint32_t>(imm64));
    EmitPush32(cursor, d_idx);
    EmitPush32(cursor, op_type);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->NeonOneRegImm())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmNeonOneRegImm::HandleOneRegImmHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
