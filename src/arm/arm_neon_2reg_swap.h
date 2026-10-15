#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VSWP (A8.8.418). */
class ArmNeon2RegSwap : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void HandleSwap(uint32_t d_idx, uint32_t m_idx, uint32_t regs);

    static void CERFENGINE_CDECL HandleSwapHelper(ArmNeon2RegSwap* svc,
                                                  uint32_t d_idx, uint32_t m_idx,
                                                  uint32_t regs);
};

}
