#include "arm_neon_2reg_bitwise_not.h"
#include <cerfengine/calling_convention.h>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegBitwiseNot);

void ArmNeon2RegBitwiseNot::HandleMvn(uint32_t d_idx, uint32_t m_idx,
                                      uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    for (uint32_t r = 0; r < regs; ++r) {
        state->vfp_d[d_idx + r] = ~state->vfp_d[m_idx + r];
    }
}

void CERFENGINE_CDECL ArmNeon2RegBitwiseNot::HandleMvnHelper(ArmNeon2RegBitwiseNot* svc,
                                                             uint32_t d_idx, uint32_t m_idx,
                                                             uint32_t regs) {
    svc->HandleMvn(d_idx, m_idx, regs);
}

}
