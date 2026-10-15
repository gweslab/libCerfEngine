#include "arm_neon_2reg_reciprocal.h"
#include <cerfengine/calling_convention.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegReciprocal);

namespace {

/* ARM DDI 0406C.c UnsignedRecipEstimate() (p. A2-85). */
uint32_t UnsignedRecipEstimate(uint32_t operand) {
    if ((operand & 0x80000000u) == 0u) {
        return 0xFFFFFFFFu;
    }
    const uint64_t dp_bits =
        (uint64_t{0x3FE} << 52) | (static_cast<uint64_t>(operand & 0x7FFFFFFFu) << 21);
    const double estimate = ArmVfp::RecipEstimate(ArmVfp::BitCast<double>(dp_bits));
    const uint64_t est_bits = ArmVfp::BitCast<uint64_t>(estimate);
    return 0x80000000u | static_cast<uint32_t>((est_bits >> 21) & 0x7FFFFFFFu);
}

/* ARM DDI 0406C.c UnsignedRSqrtEstimate() (p. A2-88). */
uint32_t UnsignedRSqrtEstimate(uint32_t operand) {
    if ((operand & 0xC0000000u) == 0u) {
        return 0xFFFFFFFFu;
    }
    uint64_t dp_bits;
    if (operand & 0x80000000u) {
        dp_bits = (uint64_t{0x3FE} << 52)
                | (static_cast<uint64_t>(operand & 0x7FFFFFFFu) << 21);
    } else {
        dp_bits = (uint64_t{0x3FD} << 52)
                | (static_cast<uint64_t>(operand & 0x3FFFFFFFu) << 22);
    }
    const double estimate = ArmVfp::RecipSqrtEstimate(ArmVfp::BitCast<double>(dp_bits));
    const uint64_t est_bits = ArmVfp::BitCast<uint64_t>(estimate);
    return 0x80000000u | static_cast<uint32_t>((est_bits >> 21) & 0x7FFFFFFFu);
}

uint32_t FPRecipEstimate(uint32_t operand_bits, uint32_t* fpscr) {
    const uint32_t sign_bit = operand_bits & 0x80000000u;
    const uint32_t exp_bits = (operand_bits >> 23) & 0xFFu;
    const uint32_t frac_bits = operand_bits & 0x7FFFFFu;

    if (exp_bits == 0xFFu && frac_bits != 0u) {
        if ((operand_bits & 0x400000u) == 0u) {
            *fpscr |= ArmVfp::kFpscrIocMask;
        }
        return 0x7FC00000u;
    }
    if (exp_bits == 0xFFu) {
        return sign_bit;
    }
    if (exp_bits == 0u) {
        if (frac_bits != 0u) {
            *fpscr |= ArmVfp::kFpscrIdcMask;
        }
        *fpscr |= ArmVfp::kFpscrDzcMask;
        return sign_bit | 0x7F800000u;
    }
    if (exp_bits >= 253u) {
        *fpscr |= ArmVfp::kFpscrUfcMask;
        return sign_bit;
    }
    const uint64_t scaled = (uint64_t{0x3FE} << 52)
                          | (static_cast<uint64_t>(frac_bits) << 29);
    const double estimate = ArmVfp::RecipEstimate(ArmVfp::BitCast<double>(scaled));
    const uint64_t est_bits = ArmVfp::BitCast<uint64_t>(estimate);
    const uint32_t result_exp = 253u - exp_bits;
    return sign_bit
         | ((result_exp & 0xFFu) << 23)
         | static_cast<uint32_t>((est_bits >> 29) & 0x7FFFFFu);
}

uint32_t FPRSqrtEstimate(uint32_t operand_bits, uint32_t* fpscr) {
    const uint32_t sign_bit  = operand_bits & 0x80000000u;
    const uint32_t exp_bits  = (operand_bits >> 23) & 0xFFu;
    const uint32_t frac_bits = operand_bits & 0x7FFFFFu;

    if (exp_bits == 0xFFu && frac_bits != 0u) {
        if ((operand_bits & 0x400000u) == 0u) {
            *fpscr |= ArmVfp::kFpscrIocMask;
        }
        return 0x7FC00000u;
    }
    if (exp_bits == 0u) {
        if (frac_bits != 0u) {
            *fpscr |= ArmVfp::kFpscrIdcMask;
        }
        *fpscr |= ArmVfp::kFpscrDzcMask;
        return sign_bit | 0x7F800000u;
    }
    if (sign_bit != 0u) {
        *fpscr |= ArmVfp::kFpscrIocMask;
        return 0x7FC00000u;
    }
    if (exp_bits == 0xFFu) {
        return 0u;
    }
    const uint64_t scaled =
        (((operand_bits & (1u << 23)) == 0u) ? (uint64_t{0x3FE} << 52)
                                             : (uint64_t{0x3FD} << 52))
        | (static_cast<uint64_t>(frac_bits) << 29);
    const double estimate = ArmVfp::RecipSqrtEstimate(ArmVfp::BitCast<double>(scaled));
    const uint64_t est_bits = ArmVfp::BitCast<uint64_t>(estimate);
    const uint32_t result_exp = (380u - exp_bits) >> 1;
    return ((result_exp & 0xFFu) << 23)
         | static_cast<uint32_t>((est_bits >> 29) & 0x7FFFFFu);
}

}

void ArmNeon2RegReciprocal::HandleReciprocal(uint32_t op_sel, uint32_t F,
                                             uint32_t d_idx, uint32_t m_idx,
                                             uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    for (uint32_t r = 0; r < regs; ++r) {
        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx + r]);
        uint8_t res[8];
        for (uint32_t e = 0; e < 2u; ++e) {
            uint32_t in;
            std::memcpy(&in, src + e * 4u, 4);
            uint32_t out;
            if (op_sel == kRecpe) {
                out = (F != 0u) ? FPRecipEstimate(in, &state->fpscr)
                                : UnsignedRecipEstimate(in);
            } else {
                out = (F != 0u) ? FPRSqrtEstimate(in, &state->fpscr)
                                : UnsignedRSqrtEstimate(in);
            }
            std::memcpy(res + e * 4u, &out, 4);
        }
        std::memcpy(&state->vfp_d[d_idx + r], res, 8);
    }
}

void CERFENGINE_CDECL ArmNeon2RegReciprocal::HandleReciprocalHelper(
        ArmNeon2RegReciprocal* svc, uint32_t op_sel, uint32_t F,
        uint32_t d_idx, uint32_t m_idx, uint32_t regs) {
    svc->HandleReciprocal(op_sel, F, d_idx, m_idx, regs);
}

}
