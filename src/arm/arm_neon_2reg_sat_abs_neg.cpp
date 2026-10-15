#include "arm_neon_2reg_sat_abs_neg.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_vfp.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegSatAbsNeg);

void ArmNeon2RegSatAbsNeg::HandleSatAbsNeg(uint32_t op_sel, uint32_t esize,
                                           uint32_t d_idx, uint32_t m_idx,
                                           uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    const uint32_t ebytes   = esize / 8u;
    const uint32_t elements = 8u / ebytes;

    const int64_t int_min = -(int64_t{1} << (esize - 1u));
    const int64_t int_max =  (int64_t{1} << (esize - 1u)) - 1;
    bool saturated = false;

    for (uint32_t r = 0; r < regs; ++r) {
        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(&state->vfp_d[m_idx + r]);
        uint8_t res[8];
        for (uint32_t e = 0; e < elements; ++e) {
            const int64_t v = ArmVfp::LoadIntS(src + e * ebytes, esize);
            int64_t result;
            if (v == int_min) {
                result    = int_max;
                saturated = true;
            } else if (op_sel == kQabs) {
                result = (v < 0) ? -v : v;
            } else {
                result = -v;
            }
            const uint64_t bits = static_cast<uint64_t>(result);
            std::memcpy(res + e * ebytes, &bits, ebytes);
        }
        std::memcpy(&state->vfp_d[d_idx + r], res, 8);
    }
    if (saturated) {
        state->fpscr |= ArmVfp::kFpscrQcMask;
    }
}

void CERFENGINE_CDECL ArmNeon2RegSatAbsNeg::HandleSatAbsNegHelper(
        ArmNeon2RegSatAbsNeg* svc, uint32_t op_sel, uint32_t esize,
        uint32_t d_idx, uint32_t m_idx, uint32_t regs) {
    svc->HandleSatAbsNeg(op_sel, esize, d_idx, m_idx, regs);
}

}
