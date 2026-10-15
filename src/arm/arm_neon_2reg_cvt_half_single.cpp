#include "arm_neon_2reg_cvt_half_single.h"
#include <cerfengine/calling_convention.h>

#include <bit>
#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegCvtHalfSingle);

namespace {


uint32_t HalfToSingle(uint16_t h, bool ahp, uint32_t* fpscr) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp  = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t frac =  static_cast<uint32_t>(h)        & 0x3FFu;

    if (exp == 0x1Fu && !ahp) {
        if (frac == 0u) return sign | 0x7F800000u;
        if ((frac & 0x200u) == 0u) *fpscr |= ArmVfp::kFpscrIocMask;
        return 0x7FC00000u;
    }
    if (exp == 0u) {
        if (frac == 0u) return sign;
        const uint32_t width = static_cast<uint32_t>(std::bit_width(frac));
        return sign | ((width + 102u) << 23)
             | ((frac << (24u - width)) & 0x7FFFFFu);
    }
    return sign | ((exp + 112u) << 23) | (frac << 13);
}

uint16_t SingleToHalf(uint32_t f, bool ahp, uint32_t* fpscr) {
    const uint32_t sign = (f >> 16) & 0x8000u;
    const uint32_t exp  = (f >> 23) & 0xFFu;
    const uint32_t frac =  f        & 0x7FFFFFu;

    if (exp == 0xFFu) {
        if (frac != 0u) {
            if (ahp) {
                *fpscr |= ArmVfp::kFpscrIocMask;
                return static_cast<uint16_t>(sign);
            }
            if ((frac & 0x400000u) == 0u) *fpscr |= ArmVfp::kFpscrIocMask;
            return 0x7E00u;
        }
        if (ahp) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return static_cast<uint16_t>(sign | 0x7FFFu);
        }
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    /* FPUnpack N == 32 (p. A2-76): fpscr_val<24> is 1, so a denormalized
       operand becomes zero and signals FPExc_InputDenorm. */
    if (exp == 0u) {
        if (frac != 0u) *fpscr |= ArmVfp::kFpscrIdcMask;
        return static_cast<uint16_t>(sign);
    }

    const uint32_t m = 0x800000u | frac;
    int32_t  biased_exp = static_cast<int32_t>(exp) - 112;
    uint32_t int_mant;
    bool     inexact;
    bool     round_up;
    if (biased_exp > 0) {
        int_mant = m >> 13;
        const uint32_t rem = m & 0x1FFFu;
        inexact  = rem != 0u;
        round_up = rem > 0x1000u || (rem == 0x1000u && (int_mant & 1u) != 0u);
    } else {
        biased_exp = 0;
        const uint32_t shift = 126u - exp;
        if (shift >= 32u) {
            int_mant = 0u;
            inexact  = true;
            round_up = false;
        } else {
            int_mant = m >> shift;
            const uint32_t rem      = m & ((1u << shift) - 1u);
            const uint32_t half_ulp = 1u << (shift - 1u);
            inexact  = rem != 0u;
            round_up = rem > half_ulp ||
                       (rem == half_ulp && (int_mant & 1u) != 0u);
        }
        if (inexact) *fpscr |= ArmVfp::kFpscrUfcMask;
    }
    if (round_up) {
        ++int_mant;
        if (int_mant == 0x400u) biased_exp = 1;
        if (int_mant == 0x800u) { ++biased_exp; int_mant >>= 1; }
    }
    if (ahp) {
        if (biased_exp >= 32) {
            *fpscr |= ArmVfp::kFpscrIocMask;
            return static_cast<uint16_t>(sign | 0x7FFFu);
        }
    } else if (biased_exp >= 31) {
        *fpscr |= ArmVfp::kFpscrOfcMask | ArmVfp::kFpscrIxcMask;
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (inexact) *fpscr |= ArmVfp::kFpscrIxcMask;
    return static_cast<uint16_t>(sign
                                 | (static_cast<uint32_t>(biased_exp) << 10)
                                 | (int_mant & 0x3FFu));
}

}

void ArmNeon2RegCvtHalfSingle::HandleCvtHalfSingle(uint32_t op_sel,
                                                   uint32_t d_idx,
                                                   uint32_t m_idx) {
    auto* state = engine_.Get<ArmCpu>().State();
    const bool ahp = (state->fpscr & ArmVfp::kFpscrAhpMask) != 0u;

    if (op_sel == kHalfToSingle) {
        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx]);
        uint8_t res[16];
        for (uint32_t e = 0; e < 4u; ++e) {
            uint16_t h;
            std::memcpy(&h, src + e * 2u, 2);
            const uint32_t v = HalfToSingle(h, ahp, &state->fpscr);
            std::memcpy(res + e * 4u, &v, 4);
        }
        std::memcpy(&state->vfp_d[d_idx],     res,     8);
        std::memcpy(&state->vfp_d[d_idx + 1], res + 8, 8);
    } else {
        uint8_t src_q[16];
        std::memcpy(src_q,     &state->vfp_d[m_idx],     8);
        std::memcpy(src_q + 8, &state->vfp_d[m_idx + 1], 8);
        uint8_t res[8];
        for (uint32_t e = 0; e < 4u; ++e) {
            uint32_t v;
            std::memcpy(&v, src_q + e * 4u, 4);
            const uint16_t h = SingleToHalf(v, ahp, &state->fpscr);
            std::memcpy(res + e * 2u, &h, 2);
        }
        std::memcpy(&state->vfp_d[d_idx], res, 8);
    }
}

void CERFENGINE_CDECL ArmNeon2RegCvtHalfSingle::HandleCvtHalfSingleHelper(
        ArmNeon2RegCvtHalfSingle* svc, uint32_t op_sel,
        uint32_t d_idx, uint32_t m_idx) {
    svc->HandleCvtHalfSingle(op_sel, d_idx, m_idx);
}

}
