#include "arm_neon_3same_fp_arith.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon3SameFpArith);

void ArmNeon3SameFpArith::Handle3SameFpArith(uint32_t op_sel, uint32_t d_idx,
                                             uint32_t n_idx, uint32_t m_idx,
                                             uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    for (uint32_t r = 0; r < regs; ++r) {
        const uint8_t* n_src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[n_idx + r]);
        const uint8_t* m_src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx + r]);
        uint8_t res[8];
        for (uint32_t e = 0; e < 2u; ++e) {
            float fn, fm;
            std::memcpy(&fn, n_src + e * 4u, 4);
            std::memcpy(&fm, m_src + e * 4u, 4);
            float result;
            switch (op_sel) {
                case kAdd: result = ArmVfp::FPAddS(fn, fm); break;
                case kSub: result = ArmVfp::FPSubS(fn, fm); break;
                case kMul: result = ArmVfp::FPMulS(fn, fm); break;
                default:
                    result = ArmVfp::FPAbsS(ArmVfp::FPSubS(fn, fm));
                    break;
            }
            std::memcpy(res + e * 4u, &result, 4);
        }
        std::memcpy(&state->vfp_d[d_idx + r], res, 8);
    }
}

void CERFENGINE_CDECL ArmNeon3SameFpArith::Handle3SameFpArithHelper(
        ArmNeon3SameFpArith* svc, uint32_t op_sel, uint32_t d_idx,
        uint32_t n_idx, uint32_t m_idx, uint32_t regs) {
    svc->Handle3SameFpArith(op_sel, d_idx, n_idx, m_idx, regs);
}

}
