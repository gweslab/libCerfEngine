#include "arm_neon_2reg_cvt_int_fp.h"
#include <cerfengine/calling_convention.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegCvtIntFp);

namespace {

inline int32_t FpToS32(float f, uint32_t frac_bits, uint32_t* fpscr) {
    const float v = ArmVfp::FlushDenormalS(f, fpscr);
    return static_cast<int32_t>(ArmVfp::FPToFixed32(
        std::ldexp(static_cast<double>(v), frac_bits), true, true, 3u, fpscr));
}

inline uint32_t FpToU32(float f, uint32_t frac_bits, uint32_t* fpscr) {
    const float v = ArmVfp::FlushDenormalS(f, fpscr);
    return ArmVfp::FPToFixed32(
        std::ldexp(static_cast<double>(v), frac_bits), false, true, 3u, fpscr);
}

}

void ArmNeon2RegCvtIntFp::HandleCvtIntFp(uint32_t op_sel, uint32_t d_idx,
                                         uint32_t m_idx, uint32_t regs,
                                         uint32_t frac_bits) {
    auto* state = engine_.Get<ArmCpu>().State();
    for (uint32_t r = 0; r < regs; ++r) {
        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx + r]);
        uint8_t res[8];
        for (uint32_t e = 0; e < 2u; ++e) {
            uint32_t in;
            std::memcpy(&in, src + e * 4u, 4);
            uint32_t out;
            if (op_sel == kIntSToFp) {
                const bool     neg = static_cast<int32_t>(in) < 0;
                const uint32_t mag = neg ? (~in + 1u) : in;
                const float f =
                    std::ldexp(ArmVfp::FixedToFP32(mag, neg, 0u, &state->fpscr),
                               -static_cast<int>(frac_bits));
                std::memcpy(&out, &f, 4);
            } else if (op_sel == kIntUToFp) {
                const float f =
                    std::ldexp(ArmVfp::FixedToFP32(in, false, 0u, &state->fpscr),
                               -static_cast<int>(frac_bits));
                std::memcpy(&out, &f, 4);
            } else {
                float f;
                std::memcpy(&f, &in, 4);
                if (op_sel == kFpToIntS) {
                    const int32_t i = FpToS32(f, frac_bits, &state->fpscr);
                    std::memcpy(&out, &i, 4);
                } else {
                    out = FpToU32(f, frac_bits, &state->fpscr);
                }
            }
            std::memcpy(res + e * 4u, &out, 4);
        }
        std::memcpy(&state->vfp_d[d_idx + r], res, 8);
    }
}

void CERFENGINE_CDECL ArmNeon2RegCvtIntFp::HandleCvtIntFpHelper(ArmNeon2RegCvtIntFp* svc,
                                                                uint32_t op_sel,
                                                                uint32_t d_idx,
                                                                uint32_t m_idx,
                                                                uint32_t regs,
                                                                uint32_t frac_bits) {
    svc->HandleCvtIntFp(op_sel, d_idx, m_idx, regs, frac_bits);
}

}
