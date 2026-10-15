#include "arm_neon_2reg_swap.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <utility>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegSwap);

void ArmNeon2RegSwap::HandleSwap(uint32_t d_idx, uint32_t m_idx, uint32_t regs) {
    auto* state = engine_.Get<ArmCpu>().State();
    /* A8.8.418: with d == m, D[d+r] is UNKNOWN. */
    for (uint32_t r = 0; r < regs; ++r) {
        std::swap(state->vfp_d[d_idx + r], state->vfp_d[m_idx + r]);
    }
}

void CERFENGINE_CDECL ArmNeon2RegSwap::HandleSwapHelper(ArmNeon2RegSwap* svc,
                                                        uint32_t d_idx, uint32_t m_idx,
                                                        uint32_t regs) {
    svc->HandleSwap(d_idx, m_idx, regs);
}

}
