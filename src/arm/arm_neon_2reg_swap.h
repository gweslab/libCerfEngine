#pragma once

#include "arm_cpu_state_service.h"

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VSWP (A8.8.418). */
class ArmNeon2RegSwap : public ArmCpuStateService {
public:
    using ArmCpuStateService::ArmCpuStateService;

    void HandleSwap(uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleSwapHelper(ArmNeon2RegSwap* svc,
                                                  uint32_t d_idx, uint32_t m_idx,
                                                  uint32_t regs);
};

}
